#include "User.h"
#include <iostream>
#include <pqxx/pqxx>

using namespace std;
using namespace pqxx;

User::User(int id, string username, string password, string nom, string prenom){
    id = id;
    username = username;
    password = password;
    nom = nom;
    prenom = prenom;

}

UserManager::UserManager(const string& conn_str){
    m_connection_string = conn_str;
}

bool UserManager::registerUser(const string& username, const string& password) {
    try {
        connection conn(m_connection_string);
        work tx(conn);

        tx.exec(
            "INSERT INTO utilisateurs (username, password) VALUES ($1, $2);",
            pqxx::params{username, password}
        );

        tx.commit();
        return true;
    } catch (const pqxx::sql_error& e) {
        cerr << "Erreur SQL (inscription) : " << e.what() << '\n';
        return false;
    }
}

bool UserManager::authenticate(const string& username, const string& password) {
    try {
        pqxx::connection conn(m_connection_string);
        pqxx::work tx(conn);

        pqxx::result res = tx.exec(
            "SELECT id FROM utilisateurs WHERE username = $1 AND password = $2;",
            params{username, password}
        );

        return !res.empty();
    } catch (const sql_error& e) {
        cerr << "Erreur SQL (authentification) : " << e.what() << '\n';
        return false;
    }
}

std::vector<User> UserManager::getAllUsers() {
    vector<User> users;
    try {
        connection conn(m_connection_string);
        work tx(conn);

        result res = tx.exec("SELECT id, username, password FROM utilisateurs ORDER BY id ASC;");

        for (const auto& row : res) {
            User u;
            u.id = row["id"].as<int>();
            u.username = row["username"].as<string>();
            u.password = row["password"].as<string>();
            users.push_back(u);
        }
    } catch (const sql_error& e) {
        cerr << "Erreur SQL (getAllUsers) : " << e.what() << '\n';
    }
    return users;
}
