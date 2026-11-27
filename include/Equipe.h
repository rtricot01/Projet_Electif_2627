#ifndef EQUIPE_H_INCLUDED
#define EQUIPE_H_INCLUDED

#include <optional>
#include <string>
#include <vector>

#include "Manager.h"

class Utilisateur;

// Une equipe de l'AS Volley : un nom, une photo et une liste de joueurs.
class Equipe {
private:
    int m_id = 0;
    std::string m_nom;
    std::vector<int> m_membres; // id des utilisateurs de l'equipe
    bool m_aUnePhoto = false;

public:
    Equipe() = default;
    Equipe(int id, std::string nom, std::vector<int> membres, bool aUnePhoto);

    int getId() const { return m_id; }
    const std::string& getNom() const { return m_nom; }
    const std::vector<int>& getMembres() const { return m_membres; }
    bool aUnePhoto() const { return m_aUnePhoto; }
    bool contient(int utilisateurId) const;
};

// Acces aux tables "equipes" et "equipe_membres".
// Creer, modifier et supprimer une equipe est reserve aux admins.
class EquipeManager : public Manager {
public:
    // Taille maximale d'une photo d'equipe (5 Mo).
    static constexpr std::size_t TAILLE_MAX_PHOTO = 5 * 1024 * 1024;

    explicit EquipeManager(std::string connexion);

    // Type de l'image ("image/jpeg", "image/png", "image/webp") d'apres ses premiers octets,
    // ou "" si ce n'est pas une image acceptee.
    static std::string typeImage(const std::string& contenu);

    Equipe creer(const Utilisateur& auteur, const std::string& nom, const std::vector<int>& membres);
    void modifier(const Utilisateur& auteur, int equipeId, const std::string& nom, const std::vector<int>& membres);
    void supprimer(const Utilisateur& auteur, int equipeId);

    std::vector<Equipe> toutes();
    std::optional<Equipe> parId(int equipeId);

    // La photo est stockee directement dans la base (les photos ne sont pas en ligne).
    void definirPhoto(const Utilisateur& auteur, int equipeId, const std::string& cheminFichier);
    // Meme chose a partir du contenu de l'image (envoi depuis le site web).
    void definirPhotoDepuisOctets(const Utilisateur& auteur, int equipeId, const std::string& contenu);
    // Contenu de la photo (rien si l'equipe n'a pas de photo).
    std::optional<std::string> photo(int equipeId);
    // Ecrit la photo de l'equipe dans un fichier. Renvoie false si l'equipe n'a pas de photo.
    bool exporterPhoto(int equipeId, const std::string& cheminFichier);
};

#endif // EQUIPE_H_INCLUDED
