#pragma once

// Keep release discovery and support destinations together. Downstream builds
// can override this compiler definition to use their own GitHub repository.
#ifndef NX_GITHUB_REPOSITORY
#define NX_GITHUB_REPOSITORY "darkwingduck32/ACNHRecomp"
#endif

namespace nx::project {
inline constexpr char repository_prefix[] = "https://github.com/" NX_GITHUB_REPOSITORY "/";
inline constexpr char release_api[] = "https://api.github.com/repos/" NX_GITHUB_REPOSITORY "/releases/latest";
inline constexpr char latest_release[] = "https://github.com/" NX_GITHUB_REPOSITORY "/releases/latest";
inline constexpr char new_issue[] = "https://github.com/" NX_GITHUB_REPOSITORY "/issues/new";
}
