# Sauvegarder, rouvrir, comparer et partager

## Sauvegarder une frame
Avec une frame en direct disponible, utilisez Capturer la frame, Fichier > Sauvegarder la frame ou Ctrl+S hors d’un éditeur de texte actif. La capture contient une frame, ses événements et le bytecode des shaders utilisés. C’est un dossier avec manifeste versionné et données binaires, pas une vidéo ni un replay GPU.

## Rouvrir hors ligne
Ouvrez Captures, actualisez la liste si nécessaire et ouvrez une entrée. La barre d’état indique une capture hors ligne. Vous pouvez examiner shaders, commandes, graphe dérivé et analyses disponibles sans le jeu. Les actions en direct et aperçus de textures sont indisponibles. Revenez à une session active pour appliquer une modification.

## Comparer
Choisissez deux sauvegardes avec les contrôles de comparaison. Les correspondances utilisent les signatures des shaders, jamais les identifiants locaux à une session. Repérez les shaders nouveaux/modifiés et changements d’activité, puis ouvrez chaque capture pour vérifier les indices. Des conditions différentes peuvent expliquer un écart ; celui-ci ne prouve pas automatiquement une régression.

## Partager un cas reproductible
Copiez le dossier entier, pas seulement capture.json. Ajoutez les buffers profonds exportés séparément si les pixels comptent. Le snapshot d’analyse IA ajoute à la frame les désassemblages déjà calculés, reconstructions et synthèse lisible. Il ne lance pas de modèle distant et ne termine pas seul les analyses absentes.

Examinez les données avant envoi : code de shader, noms de ressources et structure déduite peuvent en faire partie. Préférez la démonstration synthétique pour les captures publiques. Notez version, mode, API et question exacte à reproduire.

Si le format d’une ancienne/nouvelle capture est refusé, conservez le dossier original et utilisez une version compatible de l’application ; ne changez pas le numéro de format pour forcer la lecture. Voir [la référence du standalone](help:ref-StandaloneApp) et [les limites](help:ref-Limitations).
