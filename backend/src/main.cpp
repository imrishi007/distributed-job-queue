#include <httplib.h>

int main() {
    httplib::Server svr;
    return svr.listen("127.0.0.1", 8080);
}