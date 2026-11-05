# Profils de jeu, tags et état persistant

## Retrouver son travail
Profil du jeu regroupe l’exécutable/API/adaptateur, les shaders connus et nouveaux, statistiques et décisions persistantes. Le profil appartient au jeu ; analyses et annotations des shaders sont indexées par signature dans la base.

Donnez un nom, un tag ou une note utile à un shader lorsque les indices le permettent. Une annotation conserve sa provenance (utilisateur, dérivée ou IA), sa date et, le cas échéant, modèle/version du prompt et confiance. Réutiliser une signature ne transforme pas une proposition IA en fait mesuré.

## Conserver l’état entre sessions
L’état sauvegardé enregistre remplacements/désactivations par signature sémantique plutôt que par ID temporaire. Examinez les contrôles d’état sauvegardé avant de réappliquer un travail. Restaurer/Activer annule la modification en direct correspondante. Un shader absent peut simplement appartenir à une scène pas encore chargée. Une signature modifiée ne doit pas recevoir aveuglément un ancien remplacement.

## Garder des données portables
L’application stocke du JSON lisible et des blobs de shaders dans ses dossiers de données. Sauvegardez Database et Games, ainsi que les sources/mods à conserver. Ne supprimez pas ces dossiers pour commencer à résoudre un problème de connexion ou d’aperçu. Réglages JSON et layout INI sont séparés des analyses.

Une capture hors ligne s’examine sans connecter de jeu ; elle n’exécute pas un état sauvegardé et ne rend pas le profil actif modifiable. Pour distribuer le travail, exportez explicitement un [paquet de mod](help:mods) et vérifiez ses entrées.

Voir [la référence du stockage et des profils](help:ref-Database) pour les fichiers, signatures et provenances.
