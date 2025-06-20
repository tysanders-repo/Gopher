#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>  // Required for std::vector conversion
#include <iostream>
#include "gopher_client_lib.hpp"

namespace py = pybind11;

PYBIND11_MODULE(gopher_client, m) {
    m.doc() = "Python bindings for GopherClient";

    // Bind the Gopher struct first
    py::class_<Gopher>(m, "Gopher")
        .def(py::init<>())
        .def_readwrite("name", &Gopher::name)
        .def_readwrite("ip", &Gopher::ip)
        .def_readwrite("port", &Gopher::port)
        .def("__repr__", [](const Gopher &g) {
            return "Gopher(name='" + g.name + "', ip='" + g.ip + "', port=" + std::to_string(g.port) + ")";
        });

    py::class_<GopherClient>(m, "GopherClient")
        .def(py::init<>())
        .def("initialize", &GopherClient::initialize,
             py::arg("name"), py::arg("port") = 0)
        .def("start_broadcasting", &GopherClient::start_broadcasting)
        .def("stop_broadcasting", &GopherClient::stop_broadcasting)
        .def("get_available_gophers", &GopherClient::get_available_gophers)
        .def("start_call", &GopherClient::start_call,
             py::arg("ip"), py::arg("port"))
        .def("end_call", &GopherClient::end_call)
        .def("is_in_call", &GopherClient::is_in_call)
        .def("process_video_display", &GopherClient::process_video_display,
             "Process video display - MUST be called from main thread on macOS")
        .def("get_name", &GopherClient::get_name)
        .def("get_ip", &GopherClient::get_ip)
        .def("get_port", &GopherClient::get_port)
        .def("enable_dev_mode", &GopherClient::enable_dev_mode)
        // wrap the callback setter so Python lambdas work
        .def("set_incoming_call_callback",
             [](GopherClient &self, py::function cb){
                 // capture a Python function as the C++ callback
                 self.set_incoming_call_callback(
                     [cb](const std::string &n,
                          const std::string &ip,
                          uint16_t port) {
                         try {
                             py::gil_scoped_acquire _;           // grab GIL
                             return cb(n, ip, port).cast<bool>();
                         } catch (const std::exception& e) {
                             // Log error and return false (reject call)
                             std::cerr << "Error in incoming call callback: " << e.what() << std::endl;
                             return false;
                         }
                     }
                 );
             },
             py::arg("callback")
        );
}