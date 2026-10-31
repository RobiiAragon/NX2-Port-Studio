# Eglantine Ampere lowerer

Unmodified snapshot of `src/shader_recompiler/frontend/ampere/lower_to_maxwell.h`
from Djihads80/Eglantine commit `de83c3140e2ab6856d9a1ec1fec1cee96958d087`.
Eden-derived code; original copyright and GPL-3.0-or-later notices are preserved.
The root GPL license applies. The earlier Eden snapshot is retained separately.

The reviewed delta fixes RET link-register classification and supports 2D/array
TXD with split result registers. This is not a Metal backend by itself.
