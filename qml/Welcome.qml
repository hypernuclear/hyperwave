import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes

// The welcome window: the app's hello, its version and licence, what it is
// capturing, the hypernuclear newsletter, and the studio's other apps. It
// opens by itself after the splash (main.cpp) until "Show at startup" is
// unticked here or in the menu, and from the menu's "Welcome to hyperwave…"
// at any time. The same window either way. hyperbin's welcome window is the
// same design in that app's colours; the two are kept in step by hand.
//
// It looks like the splash it follows: the same illustration across the
// top, the blue panel (cut on a slant, as hyperbin's is) with the mark,
// logotype and version, Satoshi throughout, on a dark stage. Every control is drawn here (the Basic
// style, restyled) rather than left to the platform, so it's the same
// window on macOS and Windows.
//
// The studio's other apps: their words, colours, marks and
// any running sale come from the site's store API, so they can change
// without an app release; offline, the built-in copy below stands in.
// Nothing here is sent anywhere except the email, and only when Subscribe
// is pressed (to hypernuclear.com for its double opt-in), plus the store
// request itself (app, version, platform). Links to the site are tagged
// and counted by `welcome` (WelcomeBridge.cpp); the address never is.
ApplicationWindow {
    id: win

    /// Where the bundled art and fonts live (qrc in the app).
    property url assets: "qrc:/icons/"

    readonly property string site: "https://hypernuclear.com"
    readonly property string signupUrl: site + "/api/newsletter/signup"
    readonly property string signupSource: "hyperwave_app"
    readonly property string storeUrl: site + "/api/v1/store?app=hyperwave"
        + "&version=" + encodeURIComponent(welcome.version)
        + "&platform=" + encodeURIComponent(Qt.platform.os)
        + "&source=" + signupSource

    // --- the palette ---------------------------------------------------
    // The splash's blue and the white that sits on it, on hypershot's dark
    // stage. `brandText` is the blue lifted for small type on the stage,
    // where the brand's own mid-tone is too dim to read.
    readonly property color brand: "#267ac8"
    readonly property color brandInk: "#ffffff"
    readonly property color brandText: "#5aa6ee"
    /// The hypernuclear wordmark over the art: the blue turned up, so it
    /// holds its own against the illustration's own blues.
    readonly property color brandBright: "#45a8ff"
    readonly property color stage: "#151514"
    readonly property color card: "#1e1e1c"
    readonly property color hairline: Qt.rgba(1, 1, 1, 0.08)
    readonly property color ink: "#ffffff"
    readonly property color inkMuted: Qt.rgba(1, 1, 1, 0.68)
    readonly property color inkFaint: Qt.rgba(1, 1, 1, 0.46)
    readonly property color errorRed: "#ff6b5e"
    readonly property int pad: 28

    // Satoshi, the studio's face, compiled in from the build's own Fontshare
    // download (its licence keeps it out of the repository: CMakeLists.txt)
    // and registered by main.cpp. Static cuts of one family, so
    // `font.weight` picks between them, through the two weights below.
    readonly property string face: "Satoshi"
    /// Headings, buttons, small caps.
    readonly property int strong: Font.Bold
    /// Labels.
    readonly property int medium: Font.Medium

    /// The studio's other apps, offline: the bands the store would send on
    /// an ordinary day, without marks, loops or a sale.
    readonly property var fallbackProducts: [
        { app: "hypershot", name: "hypershot",
          tagline: qsTr("The best-in-class capture app designed to wow you and supercharge your workflows."),
          url: site + "/", cta: qsTr("Try it free"), tag: "", mac: true, windows: true,
          background: "#3449c6", foreground: "#fff6ee", markUrl: "", loopUrl: "" },
        { app: "hyperbin", name: "hyperbin",
          tagline: qsTr("Ewwww don't forget to empty your trash/recycle bin. Nasty."),
          url: site + "/hyperbin/", cta: qsTr("Get it free"), tag: qsTr("Free"), mac: true, windows: true,
          background: "#00ff79", foreground: "#000000", markUrl: "", loopUrl: "" }
    ]
    property var products: fallbackProducts
    /// idle | sending | sent | error
    property string signupState: "idle"
    property string signupError: ""

    // Named for the Dock, Mission Control, the Windows taskbar and screen
    // readers. On macOS it isn't drawn: the bar is the illustration there,
    // and the text over it was unreadable (main.cpp hides it natively).
    title: qsTr("Welcome to hyperwave")
    // On macOS the illustration runs up under the title bar, as it does
    // on the splash; the traffic lights sit over its darkened top edge.
    flags: Qt.platform.os === "osx"
           ? Qt.Dialog | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
           : Qt.Dialog
    width: 560
    height: page.implicitHeight
    minimumWidth: 560
    maximumWidth: 560
    minimumHeight: page.implicitHeight
    maximumHeight: page.implicitHeight
    color: stage
    // The window's own padding follows the safe area since Qt 6.9, which
    // with the title bar folded in would push everything down by its
    // height (and off the bottom). The art is meant to go under it.
    topPadding: 0
    bottomPadding: 0
    leftPadding: 0
    rightPadding: 0

    /// The store's answer as bands, from the same catalog hypershot's own
    /// onboarding reads: hypershot first (its page copy, its banner, and the
    /// running promotion's label as its tag, only while there is one), then
    /// every free app but this one, in the store's order. Every string is
    /// the store's own; nothing is composed here. Anything unexpected keeps
    /// the built-in bands.
    function loadProducts() {
        const xhr = new XMLHttpRequest();
        xhr.open("GET", storeUrl);
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE || xhr.status !== 200)
                return;
            try {
                const data = JSON.parse(xhr.responseText);
                const all = data.products || [];
                // A product the store sends without a banner keeps the
                // colours it has offline, if it has any.
                const band = (p, extra) => {
                    const banner = p.banner || {};
                    const known = fallbackProducts.find(f => f.app === p.app) || {};
                    return Object.assign({
                        app: p.app, name: p.name,
                        background: banner.background || known.background || "#2a2a28",
                        foreground: banner.foreground || known.foreground || "#ffffff",
                        markUrl: banner.markUrl || "",
                        loopUrl: banner.webpUrl || ""
                    }, extra);
                };
                const bands = [];
                const paid = all.filter(p => p.app === "hypershot" && p.kind === "paid")
                                .sort((a, b) => (a.order || 99) - (b.order || 99))[0];
                if (paid)
                    bands.push(band(paid, {
                        name: "hypershot",
                        tagline: (data.copy && data.copy.hypershot && data.copy.hypershot.tagline) || "",
                        url: site + "/", cta: qsTr("Try it free"),
                        tag: (data.promotion && data.promotion.label) || "",
                        // hypershot is sold for both: its bullets say "Mac + Windows".
                        mac: true, windows: true
                    }));
                // A free app is for the systems it has a download for.
                for (const p of all) {
                    if (p.kind === "free" && p.app !== "hyperwave" && p.url) {
                        const links = p.links || {};
                        bands.push(band(p, { tagline: p.tagline || "", url: p.url,
                                             cta: qsTr("Get it free"), tag: p.price || qsTr("Free"),
                                             mac: !!links.mac, windows: !!links.windows }));
                    }
                }
                if (bands.length > 0)
                    products = bands;
            } catch (e) {
                // Keep the built-in bands.
            }
        };
        xhr.send();
    }
    /// The newsletter's double opt-in, as the site's own forms do it:
    /// the honeypot left empty, 202 meaning "check your inbox".
    function subscribe() {
        const email = emailField.text.trim();
        if (email.length === 0) {
            emailField.forceActiveFocus();
            return;
        }
        if (signupState === "sending")
            return;
        signupState = "sending";
        signupError = "";
        const xhr = new XMLHttpRequest();
        xhr.open("POST", signupUrl);
        xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
        xhr.setRequestHeader("Accept", "application/json");
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return;
            if (xhr.status === 202) {
                signupState = "sent";
                welcome.newsletterSubmitted();
                return;
            }
            let message = qsTr("Something went wrong. Please try again in a moment.");
            try {
                const body = JSON.parse(xhr.responseText);
                if (body && body.message && body.message !== "check_inbox")
                    message = body.message;
            } catch (e) {
                // Not JSON (offline, or a proxy's page): the generic line.
            }
            signupError = message;
            signupState = "error";
        };
        xhr.send("email=" + encodeURIComponent(email)
                 + "&source=" + encodeURIComponent(signupSource)
                 + "&newsletter-confirm=");
    }

    onVisibleChanged: if (visible) loadProducts()
    Shortcut { sequences: [StandardKey.Cancel]; onActivated: win.close() }

    // --- pieces --------------------------------------------------------

    /// Small caps over a section.
    component Eyebrow: Text {
        font.family: win.face
        font.weight: win.strong
        font.pixelSize: 11
        font.letterSpacing: 1.1
        font.capitalization: Font.AllUppercase
        color: win.inkFaint
    }

    /// Body copy.
    component Copy: Text {
        Layout.fillWidth: true
        font.family: win.face
        font.pixelSize: 13
        lineHeight: 1.3
        wrapMode: Text.WordWrap
        color: win.inkMuted
    }

    /// The filled button: a flat face in the brand's colour with its ink, on
    /// fire. The fire burns beneath the face and comes off its top edge, with
    /// sparks and a glow round it (src/render/shaders/buttonfire.frag): low at rest, up
    /// on hover. Whatever sits above the button in its layout should be
    /// stacked over it (z), so the flames rise behind the words.
    component FillButton: AbstractButton {
        id: fb
        property color fill: win.brand
        property color label: win.brandInk
        /// 0 at rest, 1 hovered: eased, so the fire rises rather than snaps.
        property real heat: hovered && enabled ? 1 : 0
        Behavior on heat { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
        /// The fire's and the border's clock, in seconds. Only runs while the
        /// window is up: nothing burns off screen. Wraps every ten minutes,
        /// before float time loses its fine steps.
        property real clock: 0
        NumberAnimation on clock {
            from: 0
            to: 600
            duration: 600 * 1000
            loops: Animation.Infinite
            running: win.visible
        }
        implicitHeight: 38
        implicitWidth: fbText.implicitWidth + 32
        hoverEnabled: true
        opacity: enabled ? 1 : 0.45
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Item {
            ShaderEffect {
                // Room for the fire: the flames and sparks above, the glow
                // round the other sides. All in button heights.
                readonly property real side: parent.height * 1.0
                readonly property real above: parent.height * 2.4
                readonly property real below: parent.height * 1.0
                property real time: fb.clock
                property real heat: fb.heat
                property real radius: 9
                property size size: Qt.size(width, height)
                property rect face: Qt.rect(side, above, parent.width, parent.height)
                property color color: fb.fill
                x: -side
                y: -above
                width: parent.width + 2 * side
                height: above + parent.height + below
                fragmentShader: "qrc:/shaders/buttonfire.frag.qsb"
            }
            // The face, with its fresnel border and the light going round it.
            ShaderEffect {
                anchors.fill: parent
                property real time: fb.clock
                property real heat: fb.heat
                property real press: fb.down ? 1 : 0
                property real radius: 9
                property size size: Qt.size(width, height)
                property color color: fb.fill
                fragmentShader: "qrc:/shaders/buttonface.frag.qsb"
            }
        }
        contentItem: Text {
            id: fbText
            text: fb.text
            font.family: win.face
            font.weight: win.strong
            font.pixelSize: 13
            color: fb.label
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    /// The quiet button: an outline on the stage.
    component GhostButton: AbstractButton {
        id: gb
        implicitHeight: 34
        implicitWidth: gbText.implicitWidth + 28
        hoverEnabled: true
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: 9
            color: gb.down ? Qt.rgba(1, 1, 1, 0.10) : gb.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.18)
        }
        contentItem: Text {
            id: gbText
            text: gb.text
            font.family: win.face
            font.weight: win.medium
            font.pixelSize: 13
            color: win.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    /// A setting the menu also has: a green switch, its name, and a line
    /// saying what it does. `on` is the state to show; `flipped` reports
    /// the user's click, and the owner writes it back.
    component SettingSwitch: RowLayout {
        id: setting
        property bool on
        property string label
        property string detail
        signal flipped(bool on)

        Layout.fillWidth: true
        spacing: 12

        Switch {
            id: toggle
            Layout.alignment: Qt.AlignTop
            checked: setting.on
            // A click sets `checked` by hand, which breaks the binding; put
            // it back, so the switch shows what the owner ended up with
            // (the system can refuse a login item) rather than the click.
            onToggled: {
                setting.flipped(checked);
                checked = Qt.binding(() => setting.on);
            }
            padding: 0
            implicitWidth: 34
            implicitHeight: 20
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            indicator: Rectangle {
                implicitWidth: 34
                implicitHeight: 20
                radius: 10
                color: toggle.checked ? win.brand : Qt.rgba(1, 1, 1, 0.18)
                Behavior on color { ColorAnimation { duration: 140 } }
                Rectangle {
                    width: 16; height: 16; radius: 8
                    y: 2
                    x: toggle.checked ? parent.width - width - 2 : 2
                    color: toggle.checked ? win.brandInk : "#ffffff"
                    Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }
            }
            contentItem: Item {}
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: setting.label
                font.family: win.face
                font.weight: win.medium
                font.pixelSize: 13
                color: win.ink
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: setting.flipped(!toggle.checked) }
            }
            Copy {
                text: setting.detail
                color: win.inkFaint
                font.pixelSize: 11
            }
        }
    }

    /// One of the studio's apps, as hypershot's onboarding shows them: a
    /// band in the app's own colours with its mark, a panel cut on a slant
    /// playing its loop (or saying what it does, until it has one), a tag
    /// in the corner, and the way in beside it.
    component ProductBand: ColumnLayout {
        id: row
        required property var product
        readonly property color bg: product.background
        readonly property color fg: product.foreground
        readonly property bool hasLoop: loop.status === AnimatedImage.Ready
        /// A local copy of the loop, so it loops (WelcomeBridge::cachedFile).
        property string loopFile: product.loopUrl !== "" ? welcome.cachedFile(product.loopUrl) : ""
        Connections {
            target: welcome
            function onFileCached(url, localUrl) { if (url === row.product.loopUrl) row.loopFile = localUrl; }
        }
        /// The tag rides the corner ribbon when it's a word (FREE); a sale's
        /// label is longer than the corner, so it goes under the mark.
        readonly property bool saleTag: product.app === "hypershot" && product.tag !== ""
        Layout.fillWidth: true
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                id: band
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(media.width / win.loopAspect) + 2 * media.inset
                radius: 10
                color: row.bg

                // The brand column: one width down the list, so every panel
                // starts at the same x.
                Column {
                    id: brand
                    x: 16
                    width: win.brandWidth
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    // The mark as a stencil in the band's ink, so the file's
                    // own fill doesn't matter; the name until it loads.
                    Item {
                        id: markBox
                        visible: row.product.markUrl !== "" && markSource.status === Image.Ready
                        height: 18
                        width: markSource.implicitHeight > 0
                               ? height * markSource.implicitWidth / markSource.implicitHeight : 0
                        onWidthChanged: win.fitBrand(width)
                        Image {
                            id: markSource
                            anchors.fill: parent
                            source: row.product.markUrl
                            sourceSize.height: 72
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: row.fg
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: ShaderEffectSource { sourceItem: markSource; hideSource: true }
                                maskThresholdMin: 0.5
                                maskSpreadAtMin: 1.0
                            }
                        }
                    }
                    Text {
                        visible: !markBox.visible
                        text: row.product.name
                        font.family: win.face
                        font.weight: win.strong
                        font.pixelSize: 18
                        font.letterSpacing: -0.3
                        color: row.fg
                        onImplicitWidthChanged: if (visible) win.fitBrand(implicitWidth)
                    }
                    // The running sale, as the store words it.
                    Rectangle {
                        visible: row.saleTag
                        width: saleText.implicitWidth + 10
                        height: 16
                        radius: 4
                        color: "#f83b56"
                        Text {
                            id: saleText
                            anchors.centerIn: parent
                            text: String(row.product.tag).toUpperCase()
                            font.family: win.face
                            font.weight: win.strong
                            font.pixelSize: 9
                            font.letterSpacing: 0.6
                            color: "#ffffff"
                        }
                    }
                }

                Item {
                    id: media
                    readonly property real inset: 2
                    readonly property real slant: 12
                    readonly property real r: band.radius - inset
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.margins: inset
                    width: band.width - inset - (brand.x + win.brandWidth + 14) + slant / 2
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: ShaderEffectSource {
                            sourceItem: Shape {
                                width: media.width
                                height: media.height
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeColor: "transparent"
                                    fillColor: "white"
                                    startX: media.slant; startY: 0
                                    PathLine { x: media.width - media.r; y: 0 }
                                    PathArc { x: media.width; y: media.r; radiusX: media.r; radiusY: media.r }
                                    PathLine { x: media.width; y: media.height - media.r }
                                    PathArc { x: media.width - media.r; y: media.height; radiusX: media.r; radiusY: media.r }
                                    PathLine { x: 0; y: media.height }
                                    PathLine { x: media.slant; y: 0 }
                                }
                            }
                        }
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }
                    Rectangle { anchors.fill: parent; color: row.hasLoop ? "#2b2b2a" : Qt.darker(row.bg, 1.35) }
                    AnimatedImage {
                        id: loop
                        anchors.fill: parent
                        source: row.loopFile
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: false
                        playing: win.visible && status === AnimatedImage.Ready
                        onStatusChanged: if (status === AnimatedImage.Ready && implicitHeight > 0)
                                             win.fitLoop(implicitWidth / implicitHeight)
                    }
                    // No loop (yet): what it does, in its place.
                    Text {
                        visible: !row.hasLoop
                        anchors.verticalCenter: parent.verticalCenter
                        x: media.slant + 10
                        width: parent.width - x - 34
                        text: row.product.tagline
                        font.family: win.face
                        font.pixelSize: 11
                        lineHeight: 1.15
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: row.fg
                        opacity: 0.9
                    }
                    // The tag, a ribbon across the corner: FREE, or the sale.
                    Item {
                        visible: row.product.tag !== "" && !row.saleTag
                        anchors.top: parent.top
                        anchors.right: parent.right
                        width: 46
                        height: 46
                        Rectangle {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 6
                            anchors.verticalCenterOffset: -6
                            width: 92
                            height: 14
                            rotation: 45
                            color: row.bg
                            Text {
                                anchors.centerIn: parent
                                text: String(row.product.tag).toUpperCase()
                                font.family: win.face
                                font.weight: win.strong
                                font.pixelSize: 8
                                font.letterSpacing: 0.6
                                color: row.fg
                            }
                        }
                    }
                }
            }
            // The way in, in the app's own colours.
            Rectangle {
                id: cta
                Layout.preferredWidth: 112
                Layout.preferredHeight: band.height
                radius: 10
                color: ctaArea.pressed ? Qt.darker(row.bg, 1.12) : ctaArea.containsMouse ? Qt.lighter(row.bg, 1.1) : row.bg
                Behavior on color { ColorAnimation { duration: 120 } }
                // What it costs you, and which systems it's for, as the
                // website's download buttons put it.
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: row.product.cta
                        font.family: win.face
                        font.weight: win.strong
                        font.pixelSize: 13
                        color: row.fg
                    }
                    PlatformMarks {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: row.product.mac || row.product.windows
                        mac: row.product.mac
                        windows: row.product.windows
                        size: 11
                        color: row.fg
                        opacity: 0.8
                    }
                }
                MouseArea {
                    id: ctaArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        welcome.promoClicked(row.product.app);
                        welcome.openLink(row.product.url, "welcome");
                    }
                }
            }
        }
        // With a loop playing in the panel, what it does goes underneath.
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 2
            visible: row.hasLoop && row.product.tagline !== ""
            text: row.product.tagline
            font.family: win.face
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            color: win.inkMuted
        }
    }
    /// The brand column's width: the widest mark (or name) in the list.
    property real brandWidth: 0
    function fitBrand(w) { if (w > brandWidth) brandWidth = w; }
    /// The panels' shape: the squarest loop that has loaded (550 × 120
    /// until one does), so a loop shows whole rather than cropped.
    property real loopAspect: 550 / 120
    function fitLoop(aspect) { if (aspect < loopAspect) loopAspect = aspect; }
    // --- the window ----------------------------------------------------
    Rectangle {
        id: page
        width: win.width
        implicitHeight: hero.height + body.implicitHeight + footer.height
        color: win.stage

        // The illustration, cropped to its middle, fading into the stage.
        Item {
            id: hero
            width: parent.width
            height: 164
            clip: true

            // On macOS the header IS the title bar (the art runs under it),
            // and a title bar that is the window's own content doesn't move
            // the window by itself. So a drag anywhere on the header hands
            // the move to the system, as a title bar would; the traffic
            // lights are the system's and sit above this.
            DragHandler {
                target: null
                onActiveChanged: if (active) win.startSystemMove()
            }

            Image {
                anchors.fill: parent
                source: win.assets + "splash.jpg"
                fillMode: Image.PreserveAspectCrop
                verticalAlignment: Image.AlignVCenter
                sourceSize.width: 1040
                smooth: true
            }
            // Dark at the top for the title bar's controls, dark at the
            // bottom where it hands over to the stage.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.55) }
                    GradientStop { position: 0.24; color: Qt.rgba(0, 0, 0, 0.0) }
                    GradientStop { position: 0.55; color: Qt.rgba(0, 0, 0, 0.0) }
                    GradientStop { position: 1.0; color: win.stage }
                }
            }

            // The studio, as the splash signs it: its wordmark in a bright
            // blue, lower left, lifted off the art by a shadow, level with
            // the panel across from it.
            HypernuclearWordmark {
                color: win.brandBright
                width: 84
                height: width * (viewHeight / viewWidth)
                anchors.left: parent.left
                anchors.leftMargin: win.pad
                anchors.verticalCenter: panel.verticalCenter
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "#000000"
                    shadowBlur: 1.0
                    shadowOpacity: 0.85
                    shadowScale: 1.06
                }
            }

            // The corner, as hyperbin's welcome window has it: the blue
            // panel cut on a slant, the mark and logotype in white with the
            // version under them, the same padding all round.
            Item {
                id: panel
                readonly property real inset: 12
                readonly property real slant: height * 0.36
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 26
                width: slant + inset + Math.max(lockup.width, version.implicitWidth) + inset
                height: inset + lockup.height + 1 + version.implicitHeight + inset - 4
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        fillColor: win.brand
                        strokeColor: "transparent"
                        startX: 0; startY: 0
                        PathLine { x: panel.width; y: 0 }
                        PathLine { x: panel.width; y: panel.height }
                        PathLine { x: panel.slant; y: panel.height }
                        PathLine { x: 0; y: 0 }
                    }
                }
                Row {
                    id: lockup
                    spacing: 5
                    anchors.right: parent.right
                    anchors.rightMargin: panel.inset
                    anchors.top: parent.top
                    anchors.topMargin: panel.inset - 2
                    HyperwaveMark {
                        color: win.brandInk
                        height: 22
                        width: height
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    HyperwaveLogotype {
                        color: win.brandInk
                        height: 19
                        width: height * (viewWidth / viewHeight)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    id: version
                    text: "v" + welcome.version
                    color: win.brandInk
                    font.family: win.face
                    font.weight: win.medium
                    font.pixelSize: 11
                    anchors.right: lockup.right
                    anchors.top: lockup.bottom
                    anchors.topMargin: 1
                }
            }
        }
        ColumnLayout {
            id: body
            anchors.top: hero.bottom
            anchors.topMargin: -2
            x: win.pad
            width: parent.width - 2 * win.pad
            spacing: 0

            Text {
                Layout.fillWidth: true
                text: qsTr("Soothe your brain with dancing audio in your menu bar.")
                font.family: win.face
                font.weight: win.strong
                font.pixelSize: 24
                font.letterSpacing: -0.5
                wrapMode: Text.WordWrap
                color: win.ink
            }
            Copy {
                Layout.topMargin: 8
                text: qsTr("hyperwave morphs your system audio into a dancing audio visualization that ASMRs your eyeballs. 💃 "
                           + "Free and open source under the MIT license, made by your dear friends at hypernuclear. 😘")
            }
            // What the old About box always showed: what is being captured,
            // and at what rate. The first thing to check when the bars stay
            // flat.
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 6
                visible: welcome.captureStatus !== ""
                text: welcome.captureStatus
                font.family: win.face
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                color: win.inkFaint
            }

            // --- the newsletter ------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 20
                implicitHeight: signup.implicitHeight + 36
                radius: 14
                color: win.card
                border.width: 1
                border.color: win.hairline

                ColumnLayout {
                    id: signup
                    x: 20
                    y: 18
                    width: parent.width - 40
                    spacing: 0

                    Eyebrow {
                        text: qsTr("Don't miss the drop")
                        color: win.brandText
                    }
                    Copy {
                        Layout.topMargin: 8
                        text: qsTr("Rocking product drops and a weird thing or two. 😂 One click to confirm, "
                               + "unsubscribe whenever.")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        visible: win.signupState !== "sent"
                        spacing: 8
                        // Under the words above it, so the Subscribe button's
                        // flames rise behind them rather than over them.
                        z: -1

                        TextField {
                            id: emailField
                            Layout.fillWidth: true
                            // Over the button's fire, whose glow reaches it.
                            z: 1
                            implicitHeight: 38
                            leftPadding: 12
                            rightPadding: 12
                            placeholderText: qsTr("you@example.com")
                            placeholderTextColor: win.inkFaint
                            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                            enabled: win.signupState !== "sending"
                            font.family: win.face
                            font.pixelSize: 14
                            color: win.ink
                            selectionColor: win.brand
                            selectedTextColor: win.brandInk
                            verticalAlignment: TextInput.AlignVCenter
                            onAccepted: win.subscribe()
                            background: Rectangle {
                                radius: 9
                                color: "#121211"
                                border.width: emailField.activeFocus ? 2 : 1
                                border.color: emailField.activeFocus ? win.brand : Qt.rgba(1, 1, 1, 0.14)
                            }
                        }
                        FillButton {
                            text: win.signupState === "sending" ? qsTr("Sending…") : qsTr("Subscribe")
                            enabled: win.signupState !== "sending"
                            onClicked: win.subscribe()
                        }
                    }

                    // Sent: the next step, where the field was.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 14
                        visible: win.signupState === "sent"
                        spacing: 10
                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            implicitWidth: 22
                            implicitHeight: 22
                            radius: 11
                            color: win.brand
                            Shape {
                                anchors.centerIn: parent
                                width: 10; height: 8
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeColor: win.brandInk
                                    strokeWidth: 2
                                    fillColor: "transparent"
                                    capStyle: ShapePath.RoundCap
                                    joinStyle: ShapePath.RoundJoin
                                    startX: 0.5; startY: 4
                                    PathLine { x: 3.8; y: 7 }
                                    PathLine { x: 9.5; y: 1 }
                                }
                            }
                        }
                        Copy {
                            text: qsTr("Check your inbox: one click on the link we sent and you're in.")
                            color: win.ink
                            font.weight: win.medium
                        }
                    }

                    Copy {
                        Layout.topMargin: 10
                        visible: win.signupState === "error"
                        text: win.signupError
                        color: win.errorRed
                        font.pixelSize: 12
                    }
                }
            }

            // --- the studio's other apps -----------------------------------
            Eyebrow {
                Layout.topMargin: 22
                text: qsTr("More from hypernuclear")
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 10
                spacing: 10
                Repeater {
                    model: win.products
                    ProductBand {
                        required property var modelData
                        product: modelData
                    }
                }
            }

            // --- the menu's two lasting switches, here as well ---------------
            // Open at login: the system's own login item, read fresh from it
            // (WelcomeBridge). Hidden where the platform has none.
            SettingSwitch {
                Layout.topMargin: 20
                visible: welcome.openAtLoginAvailable
                on: welcome.openAtLogin
                onFlipped: (on) => welcome.openAtLogin = on
                label: qsTr("Open at login")
                detail: qsTr("Start hyperwave when you log in. Also in the menu, as Open at Login.")
            }
            // Usage data: the disclosure analytics needs, and its switch.
            SettingSwitch {
                Layout.topMargin: welcome.openAtLoginAvailable ? 14 : 20
                visible: welcome.analyticsAvailable
                on: welcome.analyticsEnabled
                onFlipped: (on) => welcome.analyticsEnabled = on
                label: qsTr("Share anonymous usage data")
                detail: qsTr("Which look and colours you pick — never anything the app hears. "
                             + "Also in the menu, as Send Anonymous Usage Data.")
            }

            Item { implicitHeight: 22 }
        }

        // --- the foot: links, and the way out ---------------------------
        Rectangle {
            id: footer
            anchors.bottom: parent.bottom
            width: parent.width
            height: 56
            color: Qt.rgba(0, 0, 0, 0.22)
            Rectangle { width: parent.width; height: 1; color: win.hairline }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: win.pad
                anchors.verticalCenter: parent.verticalCenter
                spacing: 18
                Repeater {
                    model: [
                        { label: qsTr("Website"), url: win.site + "/hyperwave/", campaign: "welcome" },
                        { label: qsTr("GitHub"), url: "https://github.com/hypernuclear/hyperwave", campaign: "" },
                        { label: qsTr("Privacy"), url: win.site + "/docs/hypershot/legal/privacy-policy/", campaign: "welcome" }
                    ]
                    Text {
                        required property var modelData
                        text: modelData.label
                        font.family: win.face
                        font.weight: win.medium
                        font.pixelSize: 12
                        color: linkHover.hovered ? win.ink : win.inkFaint
                        HoverHandler { id: linkHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: welcome.openLink(modelData.url, modelData.campaign) }
                    }
                }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: win.pad
                anchors.verticalCenter: parent.verticalCenter
                spacing: 16

                // Whether this window opens by itself after the splash. The
                // same setting as the menu's Show Welcome at Startup.
                CheckBox {
                    id: atStartup
                    anchors.verticalCenter: parent.verticalCenter
                    checked: welcome.showAtStartup
                    onToggled: welcome.showAtStartup = checked
                    padding: 0
                    spacing: 8
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    indicator: Rectangle {
                        implicitWidth: 16
                        implicitHeight: 16
                        y: (atStartup.height - height) / 2
                        radius: 4
                        color: atStartup.checked ? win.brand : "transparent"
                        border.width: atStartup.checked ? 0 : 1.5
                        border.color: atStartup.hovered ? win.inkMuted : win.inkFaint
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Shape {
                            anchors.fill: parent
                            visible: atStartup.checked
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: win.brandInk
                                strokeWidth: 2
                                fillColor: "transparent"
                                capStyle: ShapePath.RoundCap
                                joinStyle: ShapePath.RoundJoin
                                startX: 4; startY: 8.2
                                PathLine { x: 6.8; y: 11 }
                                PathLine { x: 12; y: 5.2 }
                            }
                        }
                    }
                    contentItem: Text {
                        leftPadding: atStartup.indicator.width + atStartup.spacing
                        text: qsTr("Show at startup")
                        font.family: win.face
                        font.weight: win.medium
                        font.pixelSize: 12
                        color: atStartup.hovered ? win.ink : win.inkFaint
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                GhostButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Done")
                    onClicked: win.close()
                }
            }
        }
    }
}
