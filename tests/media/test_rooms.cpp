// Host tests for modules/media-rooms.h: group_members parsing, room lookup,
// TV detection and the stale-metadata clearing rules. Run with `make test`.
#include "media-rooms.h"
#include <cstdio>
using namespace media_rooms;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
  // group_members parser
  auto v = parse_members("['media_player.living_room', 'media_player.kitchen']");
  CHECK(v.size() == 2 && v[0] == "media_player.living_room" && v[1] == "media_player.kitchen");
  CHECK(parse_members("[]").empty());
  CHECK(parse_members("").empty());
  CHECK(parse_members("None").empty());
  CHECK(parse_members("unknown").empty());
  v = parse_members("[\"media_player.a\",\"media_player.b_2\"]");
  CHECK(v.size() == 2 && v[1] == "media_player.b_2");
  v = parse_members("media_player.a, media_player.b");
  CHECK(v.size() == 2);
  v = parse_members("['media_player.a', 'media_player.a']");
  CHECK(v.size() == 1);
  v = parse_members("['media_player.office']");
  CHECK(v.size() == 1 && v[0] == "media_player.office");

  // lookup / coordinator resolution
  auto &rs = rooms();
  const char *slugs[7] = {"living_room", "kitchen", "dining_room", "office", "bedroom", "garage", "portable"};
  for (int i = 0; i < 7; i++) rs[i].slug = slugs[i];
  CHECK(coord_of(rs[0]) == "media_player.living_room");
  rs[1].members = parse_members("['media_player.living_room', 'media_player.kitchen']");
  CHECK(coord_of(rs[1]) == "media_player.living_room");
  CHECK(find_room("media_player.kitchen") == 1);
  CHECK(find_room("media_player.kitchen_2") == -1);
  rs[3].members = parse_members("['media_player.loft', 'media_player.office']");
  CHECK(resolve_room("media_player.loft") == 3);
  CHECK(resolve_room("media_player.nowhere") == -1);

  // TV / line-in from media_content_id alone
  Room r;
  set_state(r, "playing");
  set_content(r, "x-sonos-htastream:RINCON_123:spdif");
  CHECK(is_tv(r));
  CHECK(display_state(r, is_tv(r)) == "tv");
  set_content(r, "x-rincon-stream:RINCON_456");
  CHECK(is_tv(r));
  set_content(r, "x-sonos-spotify:spotify%3atrack%3a1");
  CHECK(!is_tv(r));
  CHECK(display_state(r, is_tv(r)) == "playing");
  set_state(r, "paused");
  set_content(r, "x-sonos-htastream:RINCON_123:spdif");
  CHECK(display_state(r, is_tv(r)) == "paused");

  // TV -> Spotify transition clears tv (no sticky `source`)
  Room t;
  set_state(t, "playing");
  set_content(t, "x-sonos-htastream:RINCON_123:spdif");
  CHECK(display_state(t, is_tv(t)) == "tv");
  set_content(t, "x-sonos-spotify:spotify%3atrack%3a2");
  t.title = "Song";
  CHECK(!is_tv(t) && display_state(t, is_tv(t)) == "playing");

  // New content id clears the previous track's metadata; same id keeps it
  Room m;
  set_state(m, "playing");
  set_content(m, "x-sonos-spotify:a");
  m.title = "Old"; m.artist = "Old artist"; m.art = "/api/old";
  set_content(m, "x-sonos-spotify:a");
  CHECK(m.title == "Old" && m.art == "/api/old");
  set_content(m, "x-sonos-htastream:RINCON_123:spdif");   // TV has no entity_picture
  CHECK(m.title.empty() && m.artist.empty() && m.art.empty());

  // Leaving playing/paused (idle, unavailable) clears metadata + content id
  m.title = "T"; m.art = "/api/x";
  set_state(m, "paused");
  CHECK(m.title == "T");
  set_state(m, "unavailable");
  CHECK(m.title.empty() && m.art.empty() && m.content_id.empty() && !is_tv(m));
  m.title = "T";
  set_state(m, "idle");
  CHECK(m.title.empty());

  printf(fails ? "test_rooms: FAILED %d\n" : "test_rooms: ALL PASS\n", fails);
  return fails != 0;
}
