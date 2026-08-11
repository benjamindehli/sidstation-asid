// Opens and closes the page nav on narrow screens. The whole responsive part is
// in docs.css: below the width where five links stop fitting on one line it hides
// the link row and shows the Menu button, and it only does that when the .js class
// is present (set inline in each page's head, before first paint). So with
// scripting off the plain wrapping row stays put and no link is ever unreachable.
//
// This file only tracks the open state and keeps aria-expanded honest. No resize
// handling: above the breakpoint the row is shown regardless of the open class,
// so a menu left open and then widened needs nothing done to it.
(function () {
    "use strict";
    var btn = document.querySelector(".navtoggle");
    var links = document.getElementById("navlinks");
    if (!btn || !links) return;

    function setOpen(open) {
        links.classList.toggle("open", open);
        btn.setAttribute("aria-expanded", open ? "true" : "false");
    }

    btn.addEventListener("click", function () {
        setOpen(!links.classList.contains("open"));
    });

    // Escape closes it, with focus handed back to the button rather than left
    // inside a list that is no longer on screen.
    document.addEventListener("keydown", function (e) {
        if (e.key === "Escape" && links.classList.contains("open")) {
            setOpen(false);
            btn.focus();
        }
    });
})();
