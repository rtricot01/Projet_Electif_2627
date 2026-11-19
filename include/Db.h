#ifndef DB_H_INCLUDED
#define DB_H_INCLUDED

#include <string>
#include <string_view>
#include <utility>
#include <pqxx/pqxx>

// Petits outils communs pour parler a la base de donnees.
namespace db {

// Lit l'adresse de la base dans la variable d'environnement DATABASE_URL.
// Le mot de passe ne doit JAMAIS etre ecrit dans le code (le depot est public).
// Leve une exception si la variable n'est pas definie.
std::string connectionStringFromEnv();

// Execute une requete SQL avec parametres ($1, $2, ...).
// Fonctionne avec libpqxx 7.8 (Linux) comme avec 7.10 (vcpkg / Windows).
template <typename... Args>
pqxx::result exec(pqxx::work& tx, std::string_view sql, Args&&... args)
{
#if PQXX_VERSION_MAJOR > 7 || (PQXX_VERSION_MAJOR == 7 && PQXX_VERSION_MINOR >= 9)
    return tx.exec(sql, pqxx::params{std::forward<Args>(args)...});
#else
    return tx.exec_params(pqxx::zview{sql.data(), sql.size()}, std::forward<Args>(args)...);
#endif
}

} // namespace db

#endif // DB_H_INCLUDED
