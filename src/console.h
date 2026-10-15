// Comandi seriali a 115200: info, ls, free, get <file>, rm <file>, format SI.
#pragma once

// Da chiamare di continuo (da loop()): legge la seriale ed esegue i comandi.
void console_poll();
