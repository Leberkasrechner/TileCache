#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <curl/curl.h>
#include <thread>
#include <vector>
#include <string>

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

void handle_session(tcp::socket socket, const std::string& tile_server_base_url) {
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

        std::string target{req.target().data(), req.target().size()};

        if (target.empty() || target[0] != '/' ||
            target.size() < 4 || target.substr(target.size() - 4) != ".png") {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.body() = "Invalid request path or extension\n";
            res.prepare_payload();
            http::write(socket, res);
            return;
        }

        // Remove leading slash, so "z/x/y.png"
        std::string relative_path = target.substr(1);

        fs::path local_path = relative_path;  // directly store in current dir + subdirs z/x/y.png

        std::string remote_url = tile_server_base_url + relative_path;

        fs::create_directories(local_path.parent_path());

        if (!fs::exists(local_path)) {
            std::cout << "Fetching remote tile: " << remote_url << std::endl;
            if (!fetch_tile(remote_url, local_path.string())) {
                http::response<http::string_body> res{http::status::not_found, req.version()};
                res.set(http::field::content_type, "text/plain");
                res.body() = "Tile not found remotely\n";
                res.prepare_payload();
                http::write(socket, res);
                return;
            }
        } else {
            std::cout << "Serving cached tile: " << local_path << std::endl;
        }

        std::ifstream file(local_path, std::ios::binary);
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

    } catch (const std::exception& e) {
        std::cerr << "Error in session: " << e.what() << std::endl;
    }
}

void do_accept(tcp::acceptor& acceptor, asio::io_context& ioc, const std::string& tile_server_base_url) {
    acceptor.async_accept([&acceptor, &ioc, &tile_server_base_url](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            asio::post(ioc, [sock = std::move(socket), &tile_server_base_url]() mutable {
                handle_session(std::move(sock), tile_server_base_url);
            });
        } else {
            std::cerr << "Accept error: " << ec.message() << std::endl;
        }
        do_accept(acceptor, ioc, tile_server_base_url);
    });
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <tile_server_base_url>\n";
        std::cerr << "Example: " << argv[0] << " https://tileserver.example.com/styles/osm-bright/\n";
        return 1;
    }

    std::string tile_server_base_url = argv[1];
    if (tile_server_base_url.back() != '/') {
        tile_server_base_url += '/';
    }

    try {
        unsigned int n_threads = std::thread::hardware_concurrency();
        if (n_threads == 0) n_threads = 4;

        asio::io_context ioc{static_cast<int>(n_threads)};

        tcp::acceptor acceptor{ioc, tcp::endpoint(tcp::v4(), 8080)};
        std::cout << "Tile cache server running on port 8080 with " << n_threads << " threads\n";

        do_accept(acceptor, ioc, tile_server_base_url);

        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < n_threads; ++i) {
            threads.emplace_back([&ioc]() {
                ioc.run();
            });
        }

        for (auto& t : threads) {
            t.join();
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
