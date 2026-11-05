# Exporter et utiliser un paquet de mod

## Exporter une modification voulue
1. Réalisez et vérifiez un remplacement ou une désactivation en direct.
2. Ouvrez Export de mod et relisez les actions enregistrées. La dernière décision pour un shader remplace les précédentes.
3. Cochez uniquement les entrées à distribuer. Les surbrillances magenta sont décochées car elles servent au diagnostic.
4. Complétez les métadonnées puis exportez tout le dossier .cygimod.
5. Examinez mod.json et les fichiers de shaders/sources avant partage.

Restaurer ou Activer retire la modification enregistrée correspondante. L’export décrit des décisions replace/disable ; il ne copie pas tous les shaders, un état runtime arbitraire ni un replay GPU.

## Appliquer sans l’inspecteur
CyGPUInjector.addon64 est le petit add-on ReShade qui applique les paquets exportés sans standalone ni IPC. Installez-le par le parcours normal de ReShade avec add-ons. Suivez les emplacements de la [référence des mods](help:ref-ModPackages) ; contrôlez son overlay et ReShade.log pour les messages de chargement/conflit.

La correspondance utilise le hash sémantique du shader : un nouvel ID de session ne la casse pas. Un vrai changement de code doit empêcher une entrée périmée de correspondre. Un remplacement prend effet à la création du pipeline, généralement au chargement ; activer un paquet après cela peut demander un redémarrage/recréation. Les désactivations s’appliquent aux dessins/dispatches correspondants. Si deux paquets ciblent le même shader, l’ordre alphabétique décide et un avertissement est journalisé.

## Vérifier et revenir en arrière
Testez le paquet exporté indépendamment sur une scène répétable. Gardez source et métadonnées pour le suivi. Retirez/désactivez le paquet puis redémarrez/recréez les pipelines selon le besoin pour revenir à un lancement normal. Malgré son nom, l’injecteur reste un add-on chargé par ReShade ; il ne contourne pas les restrictions d’installation.
