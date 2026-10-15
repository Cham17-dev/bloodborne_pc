# HDR pour Bloodborne (branche `hdr`)

## État

| Étape | Contenu | État |
|---|---|---|
| 1 | Sortie HDR10 (Rec.2020 + PQ) de l'image SDR du jeu, paper white réglable, menu Insert en PQ | **Validé** (15/10, MSI QD-OLED 4K, RTX 4090) : image identique au SDR, paper white OK |
| — | Mire de test HDR (`BB_HDR_TEST_PATTERN=1`) | **Validé** : ombres distinctes, couleurs justes, écrêtage vers 1000 nits (réglage de l'écran) |
| — | Mode « HDR diagnostic » : export des shaders + analyse d'une image toutes les 20 s | Codé, compile, à lancer une fois |
| 2 | Identifier le shader de tonemap du jeu | En attente des fichiers du mode diagnostic |
| 3 | Remplacer le tonemap (méthode RenoDX, `host_shaders/hdr/renodx.glsl` déjà porté) | À faire |
| 4 | Upscalers (FSR/DLSS en mode mis à l'échelle travaillent sur l'image déjà tonemappée) | À faire |

Options (launcher → Fenêtre, ou variables d'environnement) :

| Launcher | Variable | Défaut |
|---|---|---|
| Allow HDR output | `BB_HDR=1` | off |
| HDR brightness (paper white) | `BB_HDR_PAPER_WHITE=80..500` | 203 |
| HDR test pattern | `BB_HDR_TEST_PATTERN=1` | off |
| HDR diagnostic (collect shaders) | `BB_DUMP_SHADERS=1`, `BB_CAPTURE_AUTO=20`, `BB_CAPTURE_DIR=<sauvegardes>/shader/dumps/frames` | off |
| — | `BB_HDR_GAMMA22=0` : décodage sRGB au lieu de gamma 2.2 | 1 |

Le journal (`last_run.log`) contient `HDR output: on, HDR10 swapchain` quand la sortie HDR est active,
ou `HDR output: off, the display offers no HDR10 format` quand l'écran ne propose pas le HDR10.

## Fiche de test

Préparation : HDR activé dans Windows (Paramètres → Affichage → Utiliser le HDR), version de test
de la branche `hdr` (onglet Actions du fork → dernière « Windows build » réussie → Artifacts).

1. **Le jeu tourne-t-il ?** Case HDR décochée. Lancer, charger une partie, jouer une minute.
2. **Mire de test.** Cocher *Allow HDR output* et *HDR test pattern*, lancer. Quatre bandes, de haut en bas :
   1. dégradé de gris en 32 marches, du noir au blanc ;
   2. 10 carrés gris : 0,1 · 1 · 10 · 50 · 100 nits · paper white · 300 · 400 · 600 · 800 nits ;
   3. 10 carrés gris : 1000 · 1200 · 1500 · 2000 · 2500 · 3000 · 4000 · 6000 · 8000 · 10000 nits ;
   4. barres de couleur : blanc, jaune, cyan, vert, magenta, rouge, bleu, noir.

   À noter : les marches du dégradé sont-elles toutes distinctes (surtout dans le noir) ? À partir de
   quel carré les carrés suivants paraissent-ils identiques (c'est la luminosité max de l'écran) ?
   Les couleurs sont-elles franches, sans teinte bizarre ?
3. **Le jeu en HDR.** Décocher la mire, garder *Allow HDR output*. Comparer avec la case décochée :
   l'image doit être pareille qu'en SDR (ni délavée, ni trop sombre). Ouvrir le menu Insert : lisible,
   pas éblouissant ? Ajuster *paper white* si le jeu paraît terne ou trop lumineux, noter la valeur gardée.
4. **Mode diagnostic.** Cocher *HDR diagnostic*, lancer, jouer 2–3 minutes en passant par plusieurs
   endroits (rêve du chasseur, une zone extérieure, un intérieur sombre). Quitter, décocher l'option.
   Compresser le dossier `shader/dumps` du dossier de sauvegardes et l'envoyer.

À remonter : les observations des points 2 et 3, le `last_run.log` si quelque chose cloche, le zip du point 4.
