#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <curl/curl.h>
#include <thread>
#include <vector>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace fs = std::filesystem;

using tcp = asio::ip::tcp;

static size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fwrite(ptr, size, nmemb, stream);
}

bool fetch_tile(const std::string& url, const std::string& local_path) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(local_path.c_str(), "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fs::remove(local_path);
        return false;
    }

    return true;
}

void handle_session(tcp::socket socket) {
    try {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;

        http::read(socket, buffer, req);

        if (req.method() != http::verb::get) {
            http::response<http::string_body> res{http::status::method_not_allowed, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "Only GET allowed\n";
            res.prepare_payload();
            http::write(socket, res);
            return;
        }

        // <-- Fix applied here: construct std::string from string_view -->
        std::string target{ req.target().data(), req.target().size() };

        if (target.size() < 7 || target[0] != '/') {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "Invalid request format\n";
            res.prepare_payload();
            http::write(socket, res);
            return;
        }

        std::string path = target.substr(1);

        size_t first_slash = path.find('/');
        size_t second_slash = path.find('/', first_slash + 1);

        if (first_slash == std::string::npos || second_slash == std::string::npos) {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "Invalid path format\n";
            res.prepare_payload();
            http::write(socket, res);
            return;
        }

        std::string zoom = path.substr(0, first_slash);
        std::string x = path.substr(first_slash + 1, second_slash - first_slash - 1);
        std::string y_png = path.substr(second_slash + 1);

        if (y_png.size() < 5 || y_png.substr(y_png.size() - 4) != ".png") {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "Expected .png extension\n";
            res.prepare_payload();
            http::write(socket, res);
            return;
        }

        std::string y = y_png.substr(0, y_png.size() - 4);
        std::string local_filename = zoom + "-" + x + "-" + y + ".png";

        if (!fs::exists(local_filename)) {
            std::string remote_url = "https://tiles.leberkasrechner.de/styles/osm-bright/" + zoom + "/" + x + "/" + y + ".png";

            std::cout << "Fetching remote tile: " << remote_url << std::endl;

            if (!fetch_tile(remote_url, local_filename)) {
                http::response<http::string_body> res{http::status::not_found, req.version()};
                res.set(http::field::content_type, "text/plain");
                res.body() = "Tile not found remotely\n";
                res.prepare_payload();
                http::write(socket, res);
                return;
            }
        } else {
            std::cout << "Serving cached tile: " << local_filename << std::endl;
        }

        std::ifstream file(local_filename, std::ios::binary);
        if (!file) {
            http::response<http::string_body> res{http::status::internal_server_error, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "Failed to open cached file\n";
            res.prepare_payload();
            http::write(socket, res);
            return;
        }

        std::string file_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        http::response<http::vector_body<char>> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "image/png");
        res.body() = std::vector<char>(file_data.begin(), file_data.end());
        res.content_length(res.body().size());
        res.prepare_payload();

        http::write(socket, res);

    } catch (std::exception& e) {
        std::cerr << "Error in session: " << e.what() << std::endl;
    }
}

void do_accept(tcp::acceptor& acceptor, asio::io_context& ioc) {
    acceptor.async_accept([&acceptor, &ioc](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            asio::post(ioc, [sock = std::move(socket)]() mutable {
                handle_session(std::move(sock));
            });
        } else {
            std::cerr << "Accept error: " << ec.message() << std::endl;
        }
        do_accept(acceptor, ioc);
    });
}

int main() {
    try {
        unsigned int n_threads = std::thread::hardware_concurrency();
        if (n_threads == 0) n_threads = 4;

        asio::io_context ioc{static_cast<int>(n_threads)};

        tcp::acceptor acceptor{ioc, tcp::endpoint(tcp::v4(), 8080)};
        std::cout << "Multithreaded server running on port 8080 with " << n_threads << " threads\n";

        do_accept(acceptor, ioc);

        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < n_threads; ++i) {
            threads.emplace_back([&ioc]() {
                ioc.run();
            });
        }

        for (auto& t : threads) {
            t.join();
        }

    } catch (std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
    }
}
