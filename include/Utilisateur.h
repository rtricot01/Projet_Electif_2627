#ifndef UTILISATEUR_H_INCLUDED
#define UTILISATEUR_H_INCLUDED

#include <optional>
#include <string>
#include <vector>

#include "Manager.h"

// Statut d'un membre du club.
enum class Statut { Client, Admin };

std::string statutVersTexte(Statut statut);
Statut statutDepuisTexte(const std::string& texte);

// Un membre de l'AS Volley.
// L'identifiant de connexion est le numero de licence.
class Utilisateur {
private:
    int m_id = 0;
    std::string m_numeroLicence;
    std::string m_nom;
    std::string m_prenom;
    std::string m_filiere;      // filiere d'etude a l'INSA Strasbourg
    Statut m_statut = Statut::Client;
    std::string m_qrToken;      // code secret unique contenu dans le QR code du joueur

public:
    // Prefixe ecrit dans chaque QR code, pour reconnaitre nos QR codes.
    static const std::string PREFIXE_QR;

    Utilisateur() = default;
    Utilisateur(int id, std::string numeroLicence, std::string nom, std::string prenom,
                std::string filiere, Statut statut, std::string qrToken);

    int getId() const { return m_id; }
    const std::string& getNumeroLicence() const { return m_numeroLicence; }
    const std::string& getNom() const { return m_nom; }
    const std::string& getPrenom() const { return m_prenom; }
    const std::string& getFiliere() const { return m_filiere; }
    Statut getStatut() const { return m_statut; }
    const std::string& getQrToken() const { return m_qrToken; }

    bool estAdmin() const { return m_statut == Statut::Admin; }
    std::string nomComplet() const;

    // Texte a mettre dans le QR code du joueur (ex : "ASVOLLEY:3f9a...").
    std::string contenuQrCode() const;
};

// Acces a la table "utilisateurs".
class UtilisateurManager : public Manager {
public:
    explicit UtilisateurManager(std::string connexion);

    // Cree un compte. Le tout premier compte cree devient admin automatiquement.
    // Renvoie l'utilisateur cree, ou rien si la licence est deja utilisee.
    std::optional<Utilisateur> inscrire(const std::string& numeroLicence, const std::string& motDePasse,
                                        const std::string& nom, const std::string& prenom,
                                        const std::string& filiere);

    // Renvoie l'utilisateur si le numero de licence et le mot de passe sont bons.
    std::optional<Utilisateur> connecter(const std::string& numeroLicence, const std::string& motDePasse);

    std::vector<Utilisateur> tous();
    std::optional<Utilisateur> parId(int id);

    // Retrouve un joueur a partir du texte lu dans son QR code.
    std::optional<Utilisateur> parContenuQrCode(const std::string& contenuQr);

    // Admin uniquement : passer un membre en admin ou en client.
    void changerStatut(const Utilisateur& auteur, int utilisateurId, Statut statut);

    // Admin uniquement : supprimer un compte.
    void supprimer(const Utilisateur& auteur, int utilisateurId);
};

#endif // UTILISATEUR_H_INCLUDED
