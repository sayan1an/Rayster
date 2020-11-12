#ifdef GL_core_profile
#define MAT4 mat4
#else
#pragma once
#define MAT4 alignas(16) glm::mat4
#endif

#define VIEWPROJ_BLOCK \
    MAT4 projView; \
    MAT4 view; \
    MAT4 proj; \
    MAT4 viewInv; \
    MAT4 projInv; \
    MAT4 projViewPrev;