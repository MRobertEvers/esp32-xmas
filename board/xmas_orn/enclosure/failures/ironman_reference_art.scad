// Flat helmet artwork reconstructed from the user's circular red/gold reference.
// Pixel-space coordinates keep the supplied silhouette and cheek/jaw geometry readable.
// Call helmet_art_red(), helmet_art_gold(), helmet_art_eyes(); each is a raw 2D region.
// Artwork occupies approximately x=-109..109, y=0..302. Scale in the enclosing sleeve.
module helmet_pixel_frame() translate([-200,350]) scale([1,-1]) children();
module helmet_art_red() helmet_pixel_frame() polygon([
    [200,49],[223,51],[247,60],[269,75],[284,96],[295,126],[299,159],
    [308,164],[305,238],[279,263],[277,297],[243,350],[161,350],
    [127,316],[115,270],[94,239],[90,164],[103,158],[109,126],
    [121,98],[142,76],[168,58],[187,51]]);
module helmet_art_gold() helmet_pixel_frame() {
    // Upper plate: tall forehead fork and the bowed lower brow from the reference.
    polygon([[121,111],[140,92],[163,81],[181,144],[219,144],[238,81],
        [260,92],[279,110],[273,139],[272,169],[274,193],
        [251,202],[226,209],[200,213],[174,209],[150,202],[125,193],
        [128,164],[127,136]]);
    // Lower plate / cheek wings. Eyes and brow gap remain open black regions.
    polygon([[117,120],[123,154],[122,183],[120,205],[125,220],
        [149,226],[175,228],[182,218],[200,220],[218,218],[225,228],
        [251,226],[276,220],[281,205],[278,183],[278,155],[283,121],
        [286,169],[288,190],[297,207],[282,225],[266,244],[241,268],
        [230,295],[170,295],[159,268],[136,245],[120,227],[105,207],
        [113,186],[115,153]]);
    // Narrow side jaw plates and the separate rectangular chin.
    polygon([[131,248],[155,273],[164,298],[157,315],[148,294]]);
    polygon([[269,248],[245,273],[236,298],[243,315],[252,294]]);
    polygon([[171,302],[229,302],[226,329],[174,329]]);
}
module helmet_art_eyes() helmet_pixel_frame() {
    polygon([[128,202],[147,208],[174,215],[169,220],[148,218],[131,214]]);
    polygon([[272,202],[253,208],[226,215],[231,220],[252,218],[269,214]]);
}
