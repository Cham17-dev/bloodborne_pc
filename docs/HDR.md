# HDR pour Bloodborne (branche `hdr`)

## État

| Étape | Contenu | État |
|---|---|---|
| 1 | Sortie HDR10 (Rec.2020 + PQ) de l'image SDR du jeu, paper white réglable, menu Insert en PQ | **Validé** (15/10, MSI QD-OLED 4K, RTX 4090) : image identique au SDR, paper white OK |
| — | Mire de test HDR (`BB_HDR_TEST_PATTERN=1`) | **Validé** : ombres distinctes, couleurs justes, écrêtage vers 1000 nits (réglage de l'écran) |
| — | Mode « HDR diagnostic » : export des shaders (cache désactivé) + analyse d'une image toutes les 20 s, constantes sur 4 Ko, textures par dessin | Validé |
| 2 | Identifier le shader de tonemap du jeu | **Fait** — voir « Tonemap de Bloodborne » ci-dessous |
| 3a | Remplacer le shader de tonemap (`hdr/bb_tonemap.frag`, `GameShaderCode()`, option *HDR tonemap (test)*, `BB_HDR_TONEMAP=0/1/2`) | **Validé** (15/10) : mode 2 teinte la partie gauche (environ un quart de l'écran : la cible fait plus de 1920 px de large en sortie 4K, à prendre en compte), mode 1 donne une image identique au jeu. Le cache de pipelines est contourné (remplacement aussi au chargement depuis le cache). |
| 3b | Courbe HDR (méthode RenoDX, `hdr/renodx.glsl`) + garder le HDR jusqu'à l'affichage | À faire : voir le plan ci-dessous |
| 4 | Upscalers (FSR/DLSS en mode mis à l'échelle travaillent sur l'image déjà tonemappée) | À faire |

Options (launcher → Fenêtre, ou variables d'environnement) :

| Launcher | Variable | Défaut |
|---|---|---|
| Allow HDR output | `BB_HDR=1` | off |
| HDR brightness (paper white) | `BB_HDR_PAPER_WHITE=80..500` | 203 |
| HDR test pattern | `BB_HDR_TEST_PATTERN=1` | off |
| HDR diagnostic (collect shaders) | `BB_DUMP_SHADERS=1`, `BB_PIPELINE_CACHE=0`, `BB_CAPTURE_AUTO=20`, `BB_CAPTURE_DIR=<sauvegardes>/shader/dumps/frames` | off |
| — | `BB_HDR_GAMMA22=0` : décodage sRGB au lieu de gamma 2.2 | 1 |

Le journal (`last_run.log`) contient `HDR output: on, HDR10 swapchain` quand la sortie HDR est active,
ou `HDR output: off, the display offers no HDR10 format` quand l'écran ne propose pas le HDR10.

## Tonemap de Bloodborne (relevé le 15/10)

Passe `#…` à 2 dessins, cible `R8G8B8A8Unorm 1920x1080` (la cible de l'interface) :

- **Dessin 1** : vs `1ea5a07e`, ps `5c02eaf8`. Textures : `img0` 1×1 RGBA8 (multiplicateur neutralisé par les
  constantes : x = 1), `img1` **la scène HDR RGBA16F** (image remplacée par un proxy de bbport, donc absente des
  listes de l'analyseur), `img2` bloom `B10G11R11 628x530`, `img3` grain 4×4.
  Constantes (tampon slot 0, mots) : 616 = 1,5 (gain de la scène) · 832 = k (2,30 clinique, 1,97 extérieur) ·
  833 = c genou (0,46 / 0,33) · 840–851 matrice 3×4 de désaturation (varie par lieu) · 808 = 1/2,2 · 809 = 1/255 et
  810 = −0,5/255 (grain) · 617 = 1 · 744–756 : chemins neutralisés.
  Calcul : `v = scène × 1,5` ; `y = 1 − exp(−k·v)` ; `T = y·(c·(y−1)+1)²` (clampé 0–1) ; bloom `U = clamp(bloom)` ;
  écran `T + U − T·U` ; matrice couleur ; `pow(·, 1/2,2)` ; grain. Sortie en **gamma 2,2** (confirme le décodage
  2,2 de l'étape 1). Alpha = scène.a.
- **Dessin 2** : vs `0b0acf50`, ps `ed4c7668` : LUT 3D 16³ (`R32G32B32A32 16x16`, préparée par cs `200820a1`) sur
  `0x10508e0000` (RGBA8). Rôle exact à confirmer (quad de 4 indices).
- Après : copie `f7229091` vers `0x10508e0000`, AA du jeu `da1faca9` vers la cible UI, copie `57f3b8c2`, puis
  l'interface ; au début de l'image suivante, copie de la cible UI vers le tampon d'affichage (RGBA8 sRGB).

## Plan de l'étape 3

1. ~~Remplacement du shader `5c02eaf8`~~ : fait (étape 3a).
2. **Courbe** : garder le rendu SDR d'origine (`T`, écran, matrice) comme référence et étendre les hautes lumières
   avec `rd_upgrade_tonemap` / Neutwo (pic = luminosité max ÷ paper white), réglable.
3. **Garder le HDR jusqu'à l'affichage** : la cible UI, les copies, l'AA et le tampon d'affichage sont en 8 bits.
   Soit les passer en RGBA16F (comme les « resource upgrades » de RenoDX), soit encoder le HDR dans le 8 bits avec
   une courbe réversible décodée par le post-process final. À trancher en lisant le cache de textures et la
   composition de l'UI de bbport.
4. Upscalers en mode mis à l'échelle (ils lisent l'image après tonemap).

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
