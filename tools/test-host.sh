#!/usr/bin/env bash
set -eu
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
gcc=${CC:-gcc}
out="$root/build/host-tests"
mkdir -p "$out"
"$gcc" -Wall -Wextra -Werror -O2 test/host_test.c source/md5.c source/wbi.c source/jsonx.c -o "$out/host_test.exe"
"$gcc" -Wall -Wextra -Werror -std=c11 -O2 -Isource test/app_flow_test.c source/app_flow.c -o "$out/app_flow_test.exe"
"$gcc" -Wall -Wextra -Werror -std=c11 -O2 -Isource test/playback_test.c source/playback_policy.c -lm -o "$out/playback_test.exe"
"$gcc" -Wall -Wextra -Werror -std=c11 -O2 test/settings_test.c -o "$out/settings_test.exe"
"$gcc" -pipe -std=c11 -Wall -Wextra -Werror -Wno-format-truncation -Itest/stubs test/api_test.c source/api_response.c source/jsonx.c source/wbi.c source/md5.c -o "$out/api-test.exe"
"$gcc" -pipe -std=c11 -Wall -Wextra -Werror -Itest/stubs -Isource test/browse_ui_test.c source/browse_ui.c -o "$out/browse-ui-test.exe"
"$out/browse-ui-test.exe"
"$out/host_test.exe"
"$out/app_flow_test.exe"
"$out/playback_test.exe"
"$out/api-test.exe"
cd "$root"
for mode in defaults write read reset read-reset; do "$out/settings_test.exe" "$mode"; done
cd "$root"
for module in social pgc favorite_queue video_actions; do
  src="$module"
  if [ "$module" = social ] || [ "$module" = video_actions ]; then src="${module}_api"; fi
  "$gcc" -std=c11 -Wall -Wextra -Werror -Isource -Itest/stubs "test/${module}_test.c" "source/${src}.c" source/api_response.c source/jsonx.c -o "$out/${module}-test.exe"
  "$out/${module}-test.exe"
done
for module in pgc social video_actions; do
  if [ "$module" = pgc ]; then
    "$gcc" -std=c11 -Wall -Wextra -Werror -Itest/stubs -Isource test/pgc_ui_test.c source/pgc_ui.c -o "$out/pgc-ui-test.exe"
  else
    "$gcc" -std=c11 -Wall -Wextra -Wno-unused-function -Itest/stubs -Isource "test/${module}_ui_test.c" -o "$out/${module}-ui-test.exe"
  fi
  "$out/${module}-ui-test.exe"
done
ime_tests="$root/test/extra"
"$gcc" -std=c11 -Wall -Wextra -Werror -Isource "$ime_tests/ime_test.c" source/ime_policy.c -o "$out/ime-test.exe"
"$out/ime-test.exe"
"$gcc" -std=c11 -Wall -Wextra -Werror -Isource "$ime_tests/screenshot_test.c" source/screenshot.c -o "$out/screenshot-test.exe"
"$out/screenshot-test.exe"
"$gcc" -std=c11 -Wall -Wextra -Werror -Isource -Itest/stubs "$ime_tests/video_info_test.c" source/video_info.c source/api_response.c source/jsonx.c -o "$out/video-info-test.exe"
"$out/video-info-test.exe"
"$gcc" -std=c11 -Wall -Wextra -Werror -Isource -Itest/stubs "$ime_tests/identity_test.c" source/video_info.c source/api_response.c source/jsonx.c -o "$out/video-identity-test.exe"
"$out/video-identity-test.exe"
"$gcc" -std=c11 -Wall -Wextra -Werror -Isource -Itest/screenshot_stubs test/screenshot_async_test.c -o "$out/screenshot-writer-test.exe"
"$out/screenshot-writer-test.exe"
"$gcc" -std=c11 -Wall -Wextra -Werror -Isource -Itest/stubs "$root/test/extra/ime_render_test.c" source/ime_policy.c -o "$out/ime-render-test.exe"
"$out/ime-render-test.exe"
"$gcc" -std=c11 -O2 -Wall -Wextra -Werror -Isource test/history_test.c source/api_response.c source/jsonx.c -o "$out/history-test.exe"
"$out/history-test.exe"
"$gcc" -std=c11 -O2 -Wall -Wextra -Werror -Isource test/navigation_test.c source/app_flow.c source/playback_policy.c -lm -o "$out/navigation-test.exe"
"$out/navigation-test.exe"
"$gcc" -std=c11 -O2 -Wall -Wextra -Werror -Isource -Itest/stubs "$root/test/extra/regression-test.c" source/favorite_queue.c source/api_response.c source/jsonx.c -o "$out/regression-test.exe"
for case in nullable deleted sparse traversal previous-end; do "$out/regression-test.exe" "$case"; done
"$gcc" -std=c11 -O2 -Wall -Wextra -Werror -Isource -Itest/stubs "$root/test/extra/cache-test.c" source/favorite_queue.c source/api_response.c source/jsonx.c -o "$out/cache-test.exe"
"$out/cache-test.exe"
printf 'Episode-navigation host suites completed\n'  

"$gcc" -std=c11 -Wall -Wextra -Werror -Isource -Itest/stubs "$root/test/extra/home-test.c" -o "$out/home-test.exe"
"$out/home-test.exe"
printf 'Watch Later toggle host suites completed\n'

"$gcc" -std=c11 -Wall -Wextra -Werror -Isource -Itest/stubs test/favorite_pages_ui_test.c -o "$out/favorite-pages-test.exe"
"$out/favorite-pages-test.exe"
printf 'Favorite touch paging host suites completed\n'
