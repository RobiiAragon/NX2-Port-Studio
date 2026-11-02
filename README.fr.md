# NX2 Port Studio

[English](README.md) · [Español](README.es.md) · **Français** · [日本語](README.ja.md) · [简体中文](README.zh-Hans.md)

Je développe des outils natifs Switch 2 pour Apple Silicon. Avec **0.3.0**, j’ajoute l’export d’applications Mac à partir d’une révision fixe de nxnative, séparée de mon backend NVN2 expérimental.

Je vérifie le titre, la mise à jour et les identifiants complets des modules avant d’admettre **Donkey Kong Bananza, 458752**, ou **Splatoon Raiders, 65536**. Je ne transforme pas les résultats de jeu d’un autre projet en validation NX2. Mario Kart World reste prévu; je ne produis pas d’IPA iPhone/iPad.

Dans l’application, je sélectionne le dossier contenant `base/` et `updates/`, puis **Inspect dump → Prepare Mac export tools → Create Mac app**. Le RomFS de la mise à jour doit être complet. Applications est la destination proposée; je conserve les applications existantes. La première compilation peut prendre une heure. Je supprime les fichiers temporaires de cette opération après réussite ou échec et conserve un journal privé et un reçu. Un export signé ne prouve pas qu’un jeu fonctionne correctement.

Je conserve l’inspection des dumps, les espaces de développement et mes recherches NVN2. Voir le [guide d’export](docs/nx2/NATIVE_EXPORT.md), les [instructions complètes](README.md), les [sources fixées](upstreams.lock.json) et les [crédits](CREDITS.md). Je préserve les licences; aucun jeu, firmware, clé, compte, ressource Nintendo ou sauvegarde n’est publié avec Studio.
