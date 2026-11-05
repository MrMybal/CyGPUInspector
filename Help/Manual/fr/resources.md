# Ressources, aperçus et images exportées

## Trouver une image
Dans Ressources, examinez dimensions, format, usage et durée de vie. Sélectionnez une cible de rendu, un buffer de profondeur ou une texture compatible. Suivez les lecteurs/rédacteurs jusqu’aux passes et shaders. Le suivi runtime ne connaît pas forcément toutes les lectures ; la capture profonde apporte les descripteurs.

## Voir les données GPU en direct
Demandez la prévisualisation de la ressource active. L’add-on la copie dans une texture partagée que le standalone ouvre sur un adaptateur compatible. Le jeu doit continuer à produire des mises à jour. Un adaptateur différent, un format non pris en charge ou un état D3D12 inconnu peut empêcher le partage ; lisez le motif plutôt que de conclure à une image vide.

La vue gère Ajuster, zoom et déplacement. Réglages d’affichage choisit RGB/R/G/B/A, plage de valeurs, mip et tranche lorsque disponibles. Pour la profondeur, vérifiez linéarisation, hypothèses near/far et reverse-Z. Le HDR peut nécessiter une autre plage pour devenir visible. La conversion d’affichage ne modifie pas la texture du jeu.

## Sauvegarder la bonne représentation
Enregistrer en PNG écrit l’image affichée avec les conversions de canal/plage. Cela illustre une analyse mais n’exporte pas les valeurs HDR/profondeur brutes. La capture profonde écrit les buffers compatibles en PNG consultable et DDS conservant leur représentation, avec un manifeste des formats, noms et échecs. Attendez la fin de l’export avant de copier le dossier.

L’image finale en direct peut accompagner la timeline. Une frame hors ligne ordinaire ne contient pas de pixels GPU vivants : Prévisualisation ne peut pas retrouver une texture d’une session fermée. Ouvrez les fichiers PNG/DDS exportés séparément si vous en avez besoin.

Voir [le suivi des ressources](help:ref-ResourceTracking) et [le partage GPU](help:ref-GPUSharing) pour les contraintes précises de format, d’état et de durée de vie.
