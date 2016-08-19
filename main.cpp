// #include <vector>
// #include <memory>
#include <thread>
#include <fstream>      // std::ofstream
#include <opencv2/opencv.hpp>
#include <zmq.hpp>
#include "zhelpers.hpp"

class server_worker {
public:

    server_worker(zmq::context_t &ctx, int sock_type) : ctx_(ctx), worker_(ctx_, sock_type) {
    }

    void work() {
        worker_.connect("inproc://backend");
        std::cout << "Server worker initiated" << std::endl;
        int i = 0;
        try {
            while (true) {
                //while (i < 1) {

                std::string identity = s_recv(worker_); // id from worker
                s_recv(worker_); // delimiter from worker, necessary after the ID
                std::string frame_width_string = s_recv(worker_); // frame width from worker
                std::string frame_heigth_string = s_recv(worker_); // frame heigth from worker
                std::string frame_data_string = s_recv(worker_); // frame data from worker

                int frame_width = atoi(frame_width_string.c_str());
                int frame_heigth = atoi(frame_heigth_string.c_str());
                std::vector<unsigned char> frameVec(frame_data_string.begin(), frame_data_string.end());

                cv::Mat frame(cv::Size(frame_width, frame_heigth), CV_8UC3, frameVec.data());

                std::cout << "Received request from " << identity << std::endl;

                char request_string[] = "request received ok";

                s_sendmore(worker_, identity);
                s_send(worker_, request_string);
                //i++;
            }
        }        catch (std::exception &e) {
            std::cout << "Error in server worker : " << e.what() << std::endl;
        }
        worker_.disconnect("inproc://backend");
        worker_.close();
        std::cout << "Server worker terminated" << std::endl;
    }

private:
    zmq::context_t &ctx_;
    zmq::socket_t worker_;
};

//  .split server task
//  This is our server task.
//  It uses the multithreaded server model to deal requests out to a pool
//  of workers and route replies back to clients. One worker can handle
//  one request at a time but one client can talk to multiple workers at
//  once.

class server_task {
public:

    server_task(std::string portNumber) : ctx_(1), frontend_(ctx_, ZMQ_ROUTER), backend_(ctx_, ZMQ_DEALER), frontend_port_(portNumber) {
    }

    enum {
        kMaxThread = 5
    };

    void run() {

        std::cout << "Server task initiated" << std::endl;

        frontend_.bind("tcp://*:" + frontend_port_);
        backend_.bind("inproc://backend");
        std::vector<server_worker *> worker;
        std::vector<std::thread *> worker_thread;

        for (int i = 0; i < kMaxThread; ++i) {
            worker.push_back(new server_worker(ctx_, ZMQ_DEALER));
            worker_thread.push_back(new std::thread(&server_worker::work, worker[i]));
            worker_thread[i]->detach();
        }

        try {
            // Shared queue
            // When the frontend is a ZMQ_ROUTER socket, and the backend is a ZMQ_DEALER socket,
            // the proxy shall act as a shared queue that collects requests from a set of clients,
            // and distributes these fairly among a set of services. Requests shall be fair-queued
            // from frontend connections and distributed evenly across backend connections.
            // Replies shall automatically return to the client that made the original request.

            // int zmq_proxy (const void *frontend, const void *backend, const void *capture);
            // The proxy connects a frontend socket to a backend socket

            // zmq_proxy() runs in the current thread and returns only if/when the current context is closed.

            zmq::proxy(static_cast<void *> (frontend_), static_cast<void *> (backend_), nullptr);
        }        catch (std::exception &e) {
            std::cout << "Error in server task : " << e.what() << std::endl;
        }

        for (int i = 0; i < kMaxThread; ++i) {
            delete worker[i];
            delete worker_thread[i];
        }
        std::cout << "Server task terminated." << std::endl;
    }

private:
    zmq::context_t ctx_;
    zmq::socket_t frontend_;
    zmq::socket_t backend_;
    std::string frontend_port_;
};


static void show_error_on_file_and_screen(std::string error);
std::string get_selfpath();

std::string _executablePath;
std::string _portNumber;

int main(int argc, char *argv[]) {
    if (argc < 2) { // É esperado 2 argumentos: o nome do programa (por padrão é passado) e o número da porta a ser usada para receber conexões dos clientes
        show_error_on_file_and_screen("Usage: " + std::string(argv[0]) + " <PORT NUMBER>");
        exit(1);
    } else {
        _executablePath = get_selfpath();
        _portNumber = argv[1];
        std::cout << "executablePath: " << _executablePath << ", server port number: " << _portNumber << std::endl;
    }

    server_task st(_portNumber);
    std::thread t(&server_task::run, &st);

    std::cout << "Main execution will join client task and wait to finish it's execution!" << std::endl;
    t.join();
    std::cout << "Main execution finished, process terminated!" << std::endl;

    return 0;
}

static void show_error_on_file_and_screen(std::string error) {
    std::ofstream outFile("execution_error_log.txt");
    outFile << error; // put on file
    std::cerr << error << std::endl; // put on screen
}

std::string get_selfpath() {
    std::string path = "";
    int path_max = 1024;
    char buff[path_max];
    ssize_t len = readlink("/proc/self/exe", buff, sizeof (buff) - 1);

    if (len != -1) {
        buff[len] = '\0';
        size_t found;
        std::string full_name = std::string(buff);
        found = full_name.find_last_of("/\\");
        path = full_name.substr(0, found);
    }
    return path;
}