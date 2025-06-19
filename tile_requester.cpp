#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <curl/curl.h>
#include <cmath>

const double min_lon = 5.53;
const double min_lat = 47.23;
const double max_lon = 15.38;
const double max_lat = 54.96;

const int min_zoom = 0;
const int max_zoom = 17;

std::mutex cout_mutex;

int lat2tileY(double lat, int zoom) {
    double lat_rad = lat * M_PI / 180.0;
    return static_cast<int>(std::floor((1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) / 2.0 * (1 << zoom)));
}

int lon2tileX(double lon, int zoom) {
    return static_cast<int>(std::floor((lon + 180.0) / 360.0 * (1 << zoom)));
}

// Write callback to receive and discard data
size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    // Just discard the data, so no storage required
    return size * nmemb;
}

bool send_request(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // Remove CURLOPT_NOBODY to do a full GET request
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

void tile_worker(const std::vector<std::tuple<int,int,int>>& jobs, int thread_id, const std::string& base_url) {
    for (const auto& job : jobs) {
        int zoom, x, y;
        std::tie(zoom, x, y) = job;

        std::string url = base_url + std::to_string(zoom) + "/" +
                          std::to_string(x) + "/" + std::to_string(y) + ".png";

        bool success = send_request(url);

        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "[Thread " << thread_id << "] "
                  << "Requested: " << url << " -> " << (success ? "OK" : "FAIL") << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <tile_server_base_url>\n";
        std::cerr << "Example: " << argv[0] << " https://tileserver.example.com/styles/osm-bright/\n";
        return 1;
    }

    std::string tile_base_url = argv[1];
    // Ensure URL ends with '/'
    if (tile_base_url.back() != '/') {
        tile_base_url += '/';
    }

    int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    std::vector<std::tuple<int,int,int>> all_jobs;

    for (int zoom = min_zoom; zoom <= max_zoom; ++zoom) {
        int x_min = lon2tileX(min_lon, zoom);
        int x_max = lon2tileX(max_lon, zoom);
        int y_min = lat2tileY(max_lat, zoom);
        int y_max = lat2tileY(min_lat, zoom);

        int max_tile = (1 << zoom) - 1;
        if (x_min < 0) x_min = 0;
        if (x_max > max_tile) x_max = max_tile;
        if (y_min < 0) y_min = 0;
        if (y_max > max_tile) y_max = max_tile;

        for (int x = x_min; x <= x_max; ++x) {
            for (int y = y_min; y <= y_max; ++y) {
                all_jobs.emplace_back(zoom, x, y);
            }
        }
    }

    std::vector<std::vector<std::tuple<int,int,int>>> thread_jobs(num_threads);
    for (size_t i = 0; i < all_jobs.size(); ++i) {
        thread_jobs[i % num_threads].push_back(all_jobs[i]);
    }

    curl_global_init(CURL_GLOBAL_ALL);

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(tile_worker, thread_jobs[i], i, std::cref(tile_base_url));
    }

    for (auto& t : threads) {
        t.join();
    }

    curl_global_cleanup();

    std::cout << "All tile requests completed.\n";
    return 0;
}
