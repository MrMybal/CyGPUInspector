# Éditer, compiler, appliquer et restaurer le HLSL

## Préparer le document
Sélectionnez un shader dont le bytecode a été capturé et ouvrez l’onglet HLSL. Choisissez une reconstruction ou ouvrez un fichier HLSL. Ouvrir dans une fenêtre agrandit l’espace de travail. Vérifiez l’ID du shader, la provenance, le point d’entrée et le profil cible avant compilation. Sauvegardez votre source séparément pour la conserver.

L’éditeur gère UTF-8, tabulations, fins de ligne, sélection, recherche/remplacement, retour à la ligne et annulation/rétablissement. Un changement de thème ou densité conserve l’historique. Au-delà de 32 Mio, la préparation lexicale commune est désactivée explicitement, mais le texte entier reste éditable.

## Compiler d’abord
Compiler / F7 compile sans remplacer le shader en direct. Lisez les messages et différences de compatibilité. Un clic sur un diagnostic rejoint la ligne/colonne source lorsqu’elle est disponible. Corrigez les liaisons, l’étage/profil et le point d’entrée avant de poursuivre. Ctrl+S sauvegarde le texte dans l’éditeur ; hors de l’éditeur, il sauvegarde une frame.

## Appliquer volontairement
Compiler et injecter / F5 dans l’éditeur actif est une action séparée, disponible avec une session en direct. Elle compile puis applique le résultat par le pipeline de remplacement de l’add-on. L’application vérifie les signatures d’entrée/sortie et liaisons de ressources ; un refus en donne la cause. En D3D12, le DXIL signé peut nécessiter le dxil.dll correspondant.

Observez le vrai jeu dans une scène répétable. Une compilation réussie ou un acquittement IPC ne valide pas l’image. Le remplacement est indisponible sur une capture hors ligne. Restaurer rétablit le shader original ; Activer annule une désactivation. La surbrillance magenta est un remplacement de diagnostic, pas une édition permanente de source.

## Conserver et partager
Le [Profil du jeu](help:profiles) conserve un état par jeu ; [Export de mod](help:mods) crée un paquet portable explicite. Relisez les entrées : les surbrillances de diagnostic sont décochées par défaut. Annuler dans l’éditeur annule du texte, pas un remplacement déjà appliqué au jeu. Pour celui-ci, utilisez Restaurer.

Voir [le schéma du parcours](figure:1) et [la référence du remplacement](help:ref-ShaderReplacement).
