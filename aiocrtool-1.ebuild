EAPI=8

inherit toolchain-funcs git-r3

DESCRIPTION="A OCR Tool that interfaces with AI inference providers"
HOMEPAGE="https://github.com/haschka/ocr_tool"
EGIT_REPO_URI="https://github.com/haschka/ocr_tool.git"

SLOT="0"
KEYWORDS="amd64 ~x86 ~ppc"
IUSE=""

DEPEND="
    media-libs/libsdl2
    net-misc/curl
    media-libs/libpng
    dev-libs/json-c
    dev-libs/glib
"

RDEPEND="${DEPEND}"

RDEPEND="${DEPEND}"

src_compile() {
        $(tc-getCC) ocr_tool.c local_resolve.c \
	`sdl2-config --libs` `pkgconf --libs --cflags glib-2.0` \
	-lcurl -lpng -ljson-c -pthread -o ocr_tool ${CFLAGS} ${LDFLAGS}
}

src_install() {
        dobin ocr_tool
}