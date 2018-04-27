#include <array>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <stack>
#include <utility>
#include <variant>
#include <vector>
#include <zmq.hpp>
#include <queue>
#include <sys/types.h>
#include <pthread.h>

#include "zhelpers.hpp"

#include "Command.h"
#include "Database.h"
#include "DatasetBuilder.h"
#include "OnDiskDataset.h"
#include "QueryParser.h"

std::string execute_command(const SelectCommand &cmd, Task *task, Database *db) {
    std::stringstream ss;

    const Query &query = cmd.get_query();
    std::vector<std::string> out;
    db->execute(query, task, &out);
    ss << "OK\n";
    for (std::string &s : out) {
        ss << s << "\n";
    }

    return ss.str();
}

std::string execute_command(const IndexCommand &cmd, Task *task, Database *db) {
    const std::string &path = cmd.get_path();
    db->index_path(task, cmd.get_index_types(), path);

    return "OK";
}

std::string execute_command(const CompactCommand &cmd, Task *task, Database *db) {
    db->compact(task);

    return "OK";
}

std::string execute_command(const StatusCommand &cmd, Task *task, Database *db) {
    std::stringstream ss;
    const std::map<uint64_t, Task> &tasks = db->current_tasks();

    ss << "OK\n";

    for (const auto &pair : tasks) {
        const Task &ts = pair.second;
        ss << ts.id << ": " << ts.work_done << " " << ts.work_estimated << "\n";
    }

    return ss.str();
}

std::string execute_command(const TopologyCommand &cmd, Task *task, Database *db) {
    std::stringstream ss;
    const std::vector<OnDiskDataset> &datasets = db->get_datasets();

    ss << "OK\n";

    for (const auto &dataset : datasets) {
        ss << "DATASET " << dataset.get_id() << "\n";
        for (const auto &index : dataset.get_indexes()) {
            std::string index_type = get_index_type_name(index.index_type());
            ss << "INDEX " << dataset.get_id() << "." << index_type << "\n";
        }
    }

    return ss.str();
}

std::string dispatch_command(const Command &cmd, Task *task, Database *db) {
    return std::visit([db, task](const auto &cmd) { return execute_command(cmd, task, db); }, cmd);
}

std::string dispatch_command_safe(const std::string &cmd_str, Database *db) {
    try {
        Command cmd = parse_command(cmd_str);
        Task *task = db->allocate_task();
        return dispatch_command(cmd, task, db);
    } catch (std::runtime_error &e) {
        std::cout << "Command failed: " << e.what() << std::endl;
        return std::string("ERR ") + e.what() + "\n";
    }
}

struct WorkerArgs {
    int worker_nbr;
    Database *db;
};

static void *
worker_thread(void *arg) {
    auto *wa = static_cast<WorkerArgs *>(arg);

    zmq::context_t context(1);
    zmq::socket_t worker(context, ZMQ_REQ);

    s_set_id(worker);
    worker.connect("ipc://backend.ipc");

    //  Tell backend we're ready for work
    s_send(worker, "READY");

    while (1) {
        //  Read and save all frames until we get an empty frame
        //  In this example there is only 1 but it could be more
        std::string address = s_recv(worker);
        {
            std::string empty = s_recv(worker);
            assert(empty.size() == 0);
        }

        //  Get request, send reply
        std::string request = s_recv(worker);
        std::cout << "Worker: " << request << std::endl;

        std::string s = dispatch_command_safe(request, wa->db);

        s_sendmore(worker, address);
        s_sendmore(worker, "");
        s_send(worker, s);
    }
    return (NULL);
}

// LRU queue on ZMQ based on exemplary implementation by:
// Olivier Chamoux <olivier.chamoux@fr.thalesgroup.com>
int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("    %s database-file [bind-address]\n", argv[0]);
        return 1;
    }

    Database db(argv[1]);
    std::string bind_address = "tcp://127.0.0.1:9281";

    if (argc > 3) {
        std::cout << "Too many command line arguments." << std::endl;
    } else if (argc == 3) {
        bind_address = std::string(argv[2]);
    }

    //  Prepare our context and sockets
    zmq::context_t context(1);
    zmq::socket_t frontend(context, ZMQ_ROUTER);
    zmq::socket_t backend(context, ZMQ_ROUTER);

    frontend.bind(bind_address);
    backend.bind("ipc://backend.ipc");

    int worker_nbr;
    for (worker_nbr = 0; worker_nbr < 3; worker_nbr++) {
        auto *wa = new WorkerArgs{worker_nbr, &db};
        pthread_t worker;
        pthread_create(&worker, NULL, worker_thread, (void *)wa);
    }
    //  Logic of LRU loop
    //  - Poll backend always, frontend only if 1+ worker ready
    //  - If worker replies, queue worker as ready and forward reply
    //    to client if necessary
    //  - If client requests, pop next worker and send request to it
    //
    //  A very simple queue structure with known max size
    std::queue<std::string> worker_queue;

    while (1) {

        //  Initialize poll set
        zmq::pollitem_t items[] = {
                //  Always poll for worker activity on backend
                { static_cast<void *>(backend), 0, ZMQ_POLLIN, 0 },
                //  Poll front-end only if we have available workers
                { static_cast<void *>(frontend), 0, ZMQ_POLLIN, 0 }
        };
        if (worker_queue.size())
            zmq::poll(&items[0], 2, -1);
        else
            zmq::poll(&items[0], 1, -1);

        //  Handle worker activity on backend
        if (items[0].revents & ZMQ_POLLIN) {

            //  Queue worker address for LRU routing
            worker_queue.push(s_recv(backend));

            {
                //  Second frame is empty
                std::string empty = s_recv(backend);
                assert(empty.size() == 0);
            }

            //  Third frame is READY or else a client reply address
            std::string client_addr = s_recv(backend);

            //  If client reply, send rest back to frontend
            if (client_addr.compare("READY") != 0) {

                {
                    std::string empty = s_recv(backend);
                    assert(empty.size() == 0);
                }

                std::string reply = s_recv(backend);
                s_sendmore(frontend, client_addr);
                s_sendmore(frontend, "");
                s_send(frontend, reply);

                //if (--client_nbr == 0)
                //    break;
            }
        }
        if (items[1].revents & ZMQ_POLLIN) {

            //  Now get next client request, route to LRU worker
            //  Client request is [address][empty][request]
            std::string client_addr = s_recv(frontend);

            {
                std::string empty = s_recv(frontend);
                assert(empty.size() == 0);
            }

            std::string request = s_recv(frontend);

            std::string worker_addr = worker_queue.front();//worker_queue [0];
            worker_queue.pop();

            s_sendmore(backend, worker_addr);
            s_sendmore(backend, "");
            s_sendmore(backend, client_addr);
            s_sendmore(backend, "");
            s_send(backend, request);
        }
    }
    return 0;
}