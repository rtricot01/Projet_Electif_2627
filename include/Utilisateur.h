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

// Pratique du joueur : loisir ou competition (sert aux semaines A et B).
enum class TypeJoueur { Loisir, Competition };

std::string typeJoueurVersTexte(TypeJoueur type);
TypeJoueur typeJoueurDepuisTexte(const std::string& texte);

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
    bool m_compteActive = false; // faux tant que le joueur n'a pas choisi son mot de passe
    TypeJoueur m_typeDeclare = TypeJoueur::Loisir; // d'apres la liste Excel ou un admin
    bool m_dansUneEquipe = false;  // un joueur d'une equipe joue forcement en competition

public:
    // Prefixe ecrit dans chaque QR code, pour reconnaitre nos QR codes.
    static const std::string PREFIXE_QR;

    Utilisateur() = default;
    Utilisateur(int id, std::string numeroLicence, std::string nom, std::string prenom,
                std::string filiere, Statut statut, std::string qrToken, bool compteActive,
                TypeJoueur typeDeclare = TypeJoueur::Loisir, bool dansUneEquipe = false);

    int getId() const { return m_id; }
    const std::string& getNumeroLicence() const { return m_numeroLicence; }
    const std::string& getNom() const { return m_nom; }
    const std::string& getPrenom() const { return m_prenom; }
    const std::string& getFiliere() const { return m_filiere; }
    Statut getStatut() const { return m_statut; }
    const std::string& getQrToken() const { return m_qrToken; }

    bool estAdmin() const { return m_statut == Statut::Admin; }
    bool compteActive() const { return m_compteActive; }
    TypeJoueur getTypeDeclare() const { return m_typeDeclare; }
    bool estDansUneEquipe() const { return m_dansUneEquipe; }
    // Competition si la liste le dit OU si le joueur fait partie d'une equipe.
    TypeJoueur typeJoueur() const;
    std::string nomComplet() const;

    // Texte a mettre dans le QR code du joueur (ex : "ASVOLLEY:3f9a...").
    std::string contenuQrCode() const;
};

// Un joueur lu dans la liste du club (fichier exporte d'Excel).
struct JoueurImporte {
    std::string numeroLicence;
    std::string nom;
    std::string prenom;
    std::string filiere;
    std::string typeJoueur; // "loisir", "competition" ou "" (non precise dans le fichier)
};

// Bilan d'un import de la liste des joueurs.
struct ResultatImport {
    int ajoutes = 0;    // nouveaux joueurs (compte a activer)
    int misAJour = 0;   // joueurs deja presents dont le nom / la filiere ont ete mis a jour
};

// Etat du compte d'un numero de licence, pour la premiere connexion.
enum class EtatCompte {
    LicenceInconnue,   // licence absente de la liste du club
    AActiver,          // joueur dans la liste, pas encore de mot de passe
    DejaActive         // le joueur a deja choisi son mot de passe
};

// Acces a la table "utilisateurs".
//
// Fonctionnement des comptes :
//  1. un admin importe la liste des joueurs (nom + numero de licence) ;
//  2. a sa premiere connexion, le joueur saisit sa licence et choisit un mot de passe ;
//  3. ensuite il se connecte avec sa licence et son mot de passe.
class UtilisateurManager : public Manager {
public:
    // Longueur minimale d'un mot de passe.
    static constexpr std::size_t LONGUEUR_MIN_MOT_DE_PASSE = 6;

    explicit UtilisateurManager(std::string connexion);

    // Vrai s'il existe au moins un admin.
    bool existeUnAdmin();

    // Cree le compte du premier admin (uniquement quand il n'y a encore aucun admin).
    // Renvoie rien si la licence est deja utilisee.
    std::optional<Utilisateur> creerPremierAdmin(const std::string& numeroLicence, const std::string& motDePasse,
                                                 const std::string& nom, const std::string& prenom,
                                                 const std::string& filiere);

    // Admin uniquement : ajoute les joueurs de la liste du club (sans mot de passe).
    // Un joueur deja present garde son compte, son nom et sa filiere sont mis a jour.
    ResultatImport importerJoueurs(const Utilisateur& auteur, const std::vector<JoueurImporte>& joueurs);

    // Premiere connexion : indique si cette licence a un compte a activer.
    EtatCompte etatCompte(const std::string& numeroLicence);

    // Premiere connexion : enregistre le mot de passe choisi par le joueur.
    // Renvoie le joueur connecte, ou rien si le compte n'est pas a activer.
    std::optional<Utilisateur> activerCompte(const std::string& numeroLicence, const std::string& motDePasse);

    // Admin uniquement : efface le mot de passe d'un joueur qui l'a oublie.
    // Le joueur refera une "premiere connexion" pour en choisir un nouveau.
    void reinitialiserMotDePasse(const Utilisateur& auteur, int utilisateurId);

    // Renvoie l'utilisateur si le numero de licence et le mot de passe sont bons.
    std::optional<Utilisateur> connecter(const std::string& numeroLicence, const std::string& motDePasse);

    std::vector<Utilisateur> tous();
    std::optional<Utilisateur> parId(int id);

    // Retrouve un joueur a partir du texte lu dans son QR code.
    std::optional<Utilisateur> parContenuQrCode(const std::string& contenuQr);

    // Admin uniquement : passer un membre en admin ou en client.
    void changerStatut(const Utilisateur& auteur, int utilisateurId, Statut statut);

    // Admin uniquement : changer le type declare (loisir / competition) d'un joueur.
    void changerTypeJoueur(const Utilisateur& auteur, int utilisateurId, TypeJoueur type);

    // Admin uniquement : supprimer un compte.
    void supprimer(const Utilisateur& auteur, int utilisateurId);
};

#endif // UTILISATEUR_H_INCLUDED
