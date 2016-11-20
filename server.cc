#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "helper.h"

using namespace std;

atomic<uint32_t> Id(0);

mutex cout_mutex;

mutex cerr_mutex;

const uint16_t PORT_NUMBER = 2016;
const uint16_t LISTENQ = 20;
const uint16_t MAX_LINE = 1000;
const uint16_t DUMP_INTERVAL = 1;

// Presumably all the websafe characters.
// Permuting this should ensure the shortest possible url.
const string dict = 
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.~_-";

const uint16_t MAX_DIGIT = dict.length();

mutex curr_id_mutex;
vector<int> curr_id;

mutex short_hand_mutex;
map<string, string> short_hand;

// Atomatically get the current id (short url) and increment to the next id.
string get_and_increment() {
    lock_guard<mutex> lock(curr_id_mutex);
    int carry = 0;
    for (int i = 0; i < curr_id.size(); i++) {
        int d = curr_id[i];
        if (d + 1 == MAX_DIGIT) {
            curr_id[i] = 0;
            carry = 1;
        } else {
            curr_id[i]++;
            carry = 0;
            break;
        }
    }
    if (carry == 1) {
        curr_id.push_back(1);
    }
    string s = "";
    for (int i : curr_id) {
        s += dict[i];
    }
    return s;
}

// Wrap the response with headers.
void wrap_response(int connFd, stringstream& data_ss) {
    string data = data_ss.str();

    stringstream ss;
    ss << "HTTP/1.1 200 OK\r\n";
    ss << "Content-Length: " << data.length() << "\r\n";
    ss << "Connection: Closed\r\n";
    ss << "Content-Type: application/json\r\n";
    ss << "\r\n";
    ss << data << "\n";
    string response = ss.str();
    
    rio_written(connFd, (char*)response.c_str(), response.length());
}

// Handle POST request.
void handle_post(int connFd, char* buffer) {
    cmatch m;
    regex r("\\{\n\t(\"url\":\"(.*)\")\n\\}");
    regex_match(buffer, m, r);
    
    string id;
    if (m.size() == 0) {
        return;
    } else {
        id = get_and_increment();
        lock_guard<mutex> lock(short_hand_mutex);
        short_hand[id] = m[2];
    }
    stringstream data_ss;
    data_ss << "{\n\t" << m[1] << ",\n\t\"slug\":\"" << id << "\"\n}";
    
    wrap_response(connFd, data_ss);
}

// Handle GET request.
void handle_get(int connFd) {
    stringstream data_ss;
    data_ss << "[\n";
    lock_guard<mutex> lock(short_hand_mutex);
    int i = 0;
    for (auto const& it : short_hand) {
        i++;
        string short_url = it.first;
        string url = it.second;
        data_ss << "\t{\n\t\t\"url\":\"" << url << "\",\n";
        data_ss << "\t\t\"slug\":\"" << short_url << "\"\n\t}";
        if (i != short_hand.size()) {
            data_ss << ",";
        }
        data_ss << "\n";
    }
    data_ss << "]\n";

    wrap_response(connFd, data_ss);
}

// Handle GET request for a slug.
void handle_get_slug(int connFd, string slug) {
    stringstream data_ss;
    map<string, string>::iterator it;
    string url;
    {
        lock_guard<mutex> lock(short_hand_mutex);
        if ((it = short_hand.find(slug)) == short_hand.end())
            return;
        url = it->second;
    }
    data_ss << "{\n\t\"url\":\"" << url << "\",\n";
    data_ss << "\t\"slug\":\"" << slug << "\"\n}\n";
    wrap_response(connFd, data_ss);
}

// Task for a thread after a connection is established.
void handler(int connFd, int tid) {
    // Logging header indicating which thread the log came from.
    string header = "[Thread:" + to_string(tid) + "] ";

    // The read buffer.
    char buffer[MAX_LINE];

    const size_t id_len = 5;

    rio_t rp;
    rio_readinitb(&rp, connFd);

    // Read in the method (POST/GET), save it for later use.
    rio_readlineb(&rp, buffer, MAX_LINE);
    string command = buffer;

    // Read over the headers until blank line.
    int content_length = -1;    
    while (rio_readlineb(&rp, buffer, MAX_LINE) >= 0) {
        const string line = buffer;
        
        if (line == "\r\n") break;
        
        int l = get_content_length(buffer);
        if (l > 0) {
            content_length = l;
        }
    }

    // Get the request data if there is any.
    if (content_length > 0) {
        if (int i = rio_readnb(&rp, buffer, content_length) < content_length) {
            lock_guard<mutex> lock(cerr_mutex);
            cerr << header << "Didn't receive all the data!" << endl;
        }
    }
   
    buffer[content_length] = '\0';
    string slug;
    char* cmd = (char*)command.c_str();
    if (is_post(cmd)) {
        {
            lock_guard<mutex> lock(cout_mutex);
            cout << header << "Handling POST /urls" << endl;
        }
        handle_post(connFd, buffer);
    } else if (is_get(cmd)) {
        {
            lock_guard<mutex> lock(cout_mutex);
            cout << header << "Handling GET /urls" << endl;
        }
        handle_get(connFd);
    } else if ((slug =  is_get_slug(cmd)) != "") {
        {
            lock_guard<mutex> lock(cout_mutex);
            cout << header << "Handling GET /urls/" << slug << endl;
        }
        handle_get_slug(connFd, slug);
    }

    close(connFd);
}


int main(int argc, char* argv[]) {
    string header = "[Server] ";

    // Helps to identify which thread is processing the request.
    atomic<int> tid(100);

    // Create socket to listen for connections.
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (listenFd < 0) {
        lock_guard<mutex> lock(cerr_mutex);
        cerr << header << "Cannot open socket" << endl;
        return 0;
    }
   
    struct sockaddr_in serverAddr;
 
    // Clear the structs.
    bzero((char*)&serverAddr, sizeof(serverAddr));
   
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(PORT_NUMBER);
    
    // Bind the socket.
    if(::bind(listenFd, 
              (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        lock_guard<mutex> lock(cerr_mutex);
        cerr << header << "Cannot bind" << endl;
        return 0;
    }
    
    // Listen with queue size at least LISTENQ. 
    if (listen(listenFd, LISTENQ) < 0)
        return -1;
    
    struct sockaddr_in clientAddr;
    socklen_t len = sizeof(clientAddr);
    
    curr_id.push_back(0);
 
    while (true) {
        {
            lock_guard<mutex> lock(cout_mutex);
            cout << header << "Listening..." << endl;
        }
        int connFd = accept(listenFd, (struct sockaddr *)&clientAddr, &len);
                
        if (connFd < 0) {
            lock_guard<mutex> lock(cerr_mutex);
            cerr << header << "Cannot accept connection" << endl;
        } else {
            char* haddrp = inet_ntoa(clientAddr.sin_addr);
            uint16_t client_port = ntohs(clientAddr.sin_port);
            lock_guard<mutex> lock(cout_mutex);
            cout << header << "Connected to: " << haddrp << ", " 
                 << client_port << endl;
        }

        int newtid = ++tid;
        thread t1(handler, connFd, newtid);
        t1.detach();
    }
}
