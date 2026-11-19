#ifndef INIT_H_INCLUDED
#define INIT_H_INCLUDED

#include <string>

// Cree (ou met a jour) toutes les tables de la base au demarrage.
class Init
{
    private:
        std::string m_connection_string;
    public:
        explicit Init(const std::string& conn_str);
        void initTables();
        std::string getConnectionString() const;
};

#endif // INIT_H_INCLUDED
