#include "../External/nlohmann/json.hpp"

using nlohmann::json;
#include <iostream>
#include <fstream>

static const char* logpath = "C:\\cool.txt";

int main()
{
    std::ofstream myfile;
    myfile.open(logpath);

    json j;
    std::cin >> j;



    std::cout << "Hello World!\n";
    myfile.close();

}

