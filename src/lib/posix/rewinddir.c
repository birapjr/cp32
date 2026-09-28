#include "dirent.h"

CP32_IRAM_EXT int cp32_rewinddir(struct cp32_dir *dir)
{
  return cp32_seekdir(dir,0);
}
