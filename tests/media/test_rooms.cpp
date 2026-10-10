// Host tests for modules/media-rooms.h: group_members parsing, room lookup,
// TV detection and the stale-metadata rules. Run with `make test`.
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

  // Same-album consecutive tracks: HA re-sends only the changed title, so a
  // content id change must keep artist/art
  Room m;
  set_state(m, "playing");
  set_content(m, "x-sonos-spotify:track1");
  m.title = "Track 1"; m.artist = "Band"; m.art = "/api/media_player_proxy/x?cache=album";
  set_content(m, "x-sonos-spotify:track2");
  m.title = "Track 2";
  CHECK(m.artist == "Band" && m.art == "/api/media_player_proxy/x?cache=album" && m.title == "Track 2");

  // The cache never clears: idle hides metadata (media_loaded false, so
  // readers publish blanks) but resuming the same track — which HA does not
  // re-send — shows it again
  set_state(m, "paused");
  CHECK(media_loaded(m));
  for (const char *st : {"idle", "off", "unavailable", "unknown"}) {
    set_state(m, st);
    CHECK(!media_loaded(m));
    CHECK(m.title == "Track 2" && m.artist == "Band" && !m.art.empty());
  }
  set_state(m, "playing");
  CHECK(media_loaded(m) && m.title == "Track 2" && m.art == "/api/media_player_proxy/x?cache=album");

  // TV room going idle and back: the constant htastream id (not re-sent by
  // HA) still marks it tv
  Room tv;
  set_state(tv, "playing");
  set_content(tv, "x-sonos-htastream:RINCON_123:spdif");
  set_state(tv, "idle");
  set_state(tv, "playing");
  CHECK(display_state(tv, is_tv(tv)) == "tv");

  printf(fails ? "test_rooms: FAILED %d\n" : "test_rooms: ALL PASS\n", fails);
  return fails != 0;
}
