#include "Init.h"
#include <iostream>
#include <pqxx/pqxx>

using namespace std ;
using namespace pqxx;


Init::Init(const string& conn_str){
    m_connection_string = conn_str;
}

string Init::getConnectionString() const {
    return m_connection_string;
}

void Init::initTable() {
    try {
        connection conn(m_connection_string);
        work tx(conn);

        tx.exec(
            "CREATE TABLE IF NOT EXISTS utilisateurs ("
            "  id SERIAL PRIMARY KEY,"
            "  username VARCHAR(50) UNIQUE NOT NULL,"
            "  password VARCHAR(100) NOT NULL"
            ");"
        );

        tx.commit();
        cout << "[BDD] Table 'utilisateurs' pr�te.\n";
    } catch (const sql_error& e) {
        cerr << "Erreur SQL lors de l'init : " << e.what() << '\n';
        throw;
    }
}

