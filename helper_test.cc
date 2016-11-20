#include <assert.h>

#include <iostream>
#include <map>
#include <string>

#include "helper.h"

using namespace std;

void test_int_byte_conversions() {
    char s[4];
    for (int i = -2000; i < 2000; i++) {
        int_to_byte(s, i);
        assert(i == byte_to_int(s));
    }
    cout << "Passed int byte conversion!" << endl;
}

void test_request() {
    string s = "POST /urls http\r\n";
    assert(is_post((char*)s.c_str()));
    assert(!is_get((char*)s.c_str()));
    s = "GET /urls http\r\n";
    assert(!is_post((char*)s.c_str()));
    assert(is_get((char*)s.c_str()));
    assert(is_get_slug((char*)s.c_str()) == "");
    s = "GET /urls/abc123ABC-_~. http\r\n";
    assert(!is_post((char*)s.c_str()));
    assert(!is_get((char*)s.c_str()));
    assert(is_get_slug((char*)s.c_str()) == "abc123ABC-_~.");
    s = "Content-Length: 32\r\n";
    assert(get_content_length((char*)s.c_str()) == 32);
    s = "random thing\r\n";
    assert(get_content_length((char*)s.c_str()) == -1);
}


int main(int argc, char* argv[]) {
    test_int_byte_conversions();
    test_request();
    cout << "All tests passed!" << endl;
}
