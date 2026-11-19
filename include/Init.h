#ifndef INIT_H_INCLUDED
#define INIT_H_INCLUDED

#include <string>
using namespace std;

class Init
{
    private:
        string m_connection_string;
    public:
        void initTable();
        explicit Init(const std::string& conn_str);
        string getConnectionString() const;
};

#endif // INIT_H_INCLUDED
