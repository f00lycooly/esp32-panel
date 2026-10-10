// Host tests for modules/media-rooms.h topology: the panel-side rebuild vs the
// old sonos_topology Jinja semantics. Run with `make test`.
#include "media-rooms.h"
#include <cstdio>
using namespace media_rooms;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { printf("FAIL %s:%d %s\n  got:  [%s]\n  want: [%s]\n", __FILE__, __LINE__, #a, std::string(_a).c_str(), std::string(_b).c_str()); fails++; } } while (0)

static void reset() {
  const char *slugs[7] = {"living_room", "kitchen", "dining_room", "office", "bedroom", "garage", "portable"};
  const char *caps[7] = {"Living Rm", "Kitchen", "Dining Rm", "Office", "Bedroom", "Garage", "Portable"};
  for (int i = 0; i < 7; i++) { rooms()[i] = Room(); rooms()[i].slug = slugs[i]; rooms()[i].caption = caps[i]; }
}
static const char *TV = "x-sonos-htastream:RINCON_1:spdif";
static void set(int i, const char *st, const char *gm, float vol, const char *cid = "",
                const char *title = "", const char *artist = "", const char *art = "") {
  Room &r = rooms()[i];
  r.state = st; r.members = parse_members(gm); r.volume = vol; r.content_id = cid;
  r.title = title; r.artist = artist; r.art = art;
}

int main() {
  // ── Scenario A: mixed house ──
  reset();
  set(0, "playing", "['media_player.living_room', 'media_player.kitchen']", 0.35f, "x-sonos-spotify:abc", "Song", "Band", "/api/media_player_proxy/media_player.living_room?token=t");
  set(1, "playing", "['media_player.living_room', 'media_player.kitchen']", 0.5f, "x-sonos-spotify:abc", "Song", "Band", "/api/x");
  set(2, "idle", "['media_player.dining_room']", 0.2f);
  set(3, "playing", "['media_player.office']", 0.105f, "", "Pod | Cast", "Host", "");
  set(4, "paused", "['media_player.bedroom']", 0.0f);
  set(5, "playing", "['media_player.garage']", 0.4f, TV);
  set(6, "playing", "['media_player.loft', 'media_player.portable']", 0.6f, "", "Loft song", "Loft artist", "");

  CHECK_EQ(room_status(0).info(), std::string("playing|1|2"));
  CHECK(room_status(0).vol == 35);
  CHECK_EQ(room_status(1).info(), std::string("playing|0|2"));
  CHECK_EQ(room_status(2).info(), std::string("idle|1|1"));
  CHECK(room_status(3).vol == 11);  // 10.5 rounds half up like Jinja round()
  CHECK_EQ(room_status(4).info(), std::string("paused|1|1"));
  CHECK_EQ(room_status(5).info(), std::string("tv|1|1"));
  CHECK_EQ(room_status(6).info(), std::string("playing|0|2"));

  auto gs = playing_groups();
  CHECK(gs.size() == 3);  // living_room, loft (inferred), office — sorted by entity id; garage is TV, bedroom paused
  if (gs.size() == 3) {
    CHECK_EQ(gs[0], std::string("living_room|playing|35|Living Rm + Kitchen|living_room,kitchen|Song|Band|/api/media_player_proxy/media_player.living_room?token=t"));
    CHECK_EQ(gs[1], std::string("loft|playing|60|Loft + Portable|loft,portable|Loft song|Loft artist|"));
    CHECK_EQ(gs[2], std::string("office|playing|11|Office|office|Pod / Cast|Host|"));
  }

  // ── TV judged on the coordinator: members of a TV group are `tv` too ──
  reset();
  set(0, "playing", "['media_player.living_room', 'media_player.kitchen']", 0.3f, TV);
  set(1, "playing", "['media_player.living_room', 'media_player.kitchen']", 0.3f, "");
  CHECK_EQ(room_status(0).info(), std::string("tv|1|2"));
  CHECK_EQ(room_status(1).info(), std::string("tv|0|2"));
  CHECK(playing_groups().empty());
  // ... and switching the coordinator back to music clears tv for the group
  set_content(rooms()[0], "x-sonos-spotify:abc");
  CHECK_EQ(room_status(0).info(), std::string("playing|1|2"));
  CHECK_EQ(room_status(1).info(), std::string("playing|0|2"));
  CHECK(playing_groups().size() == 1);

  // ── No group_members (non-Sonos / not yet reported) → own coordinator, size 1 ──
  reset();
  set(2, "playing", "", NAN, "", "T", "A", "");
  CHECK_EQ(room_status(2).info(), std::string("playing|1|1"));
  CHECK(room_status(2).vol == 0);
  gs = playing_groups();
  CHECK(gs.size() == 1);
  if (gs.size() == 1) CHECK_EQ(gs[0], std::string("dining_room|playing|0|Dining Rm|dining_room|T|A|"));

  // ── Configured "coordinator" that does not lead its own group is not a group ──
  reset();
  set(1, "playing", "['media_player.living_room', 'media_player.kitchen']", 0.3f);   // stale: thinks LR leads
  set(0, "playing", "['media_player.office', 'media_player.living_room']", 0.3f);    // LR is actually under office
  set(3, "playing", "['media_player.office', 'media_player.living_room']", 0.7f, "", "X", "Y", "");
  gs = playing_groups();
  CHECK(gs.size() == 1);
  if (gs.size() == 1) CHECK_EQ(gs[0], std::string("office|playing|70|Office + Living Rm|office,living_room|X|Y|"));

  // ── More than 4 playing groups: all returned (count), callers keep 4 ──
  reset();
  for (int i = 0; i < 7; i++) set(i, "playing", "", 0.1f);
  gs = playing_groups();
  CHECK(gs.size() == 7);
  if (gs.size() == 7) {
    CHECK(gs[0].rfind("bedroom|", 0) == 0);
    CHECK(gs[6].rfind("portable|", 0) == 0);
  }

  // ── Unconfigured slot (empty slug) is ignored ──
  reset();
  rooms()[4].slug = "";
  set(4, "playing", "", 0.1f);
  CHECK(playing_groups().empty());

  CHECK_EQ(pretty_slug("living_room"), std::string("Living Room"));
  CHECK_EQ(slug_of("media_player.kitchen_2"), std::string("kitchen_2"));

  printf(fails ? "test_topology: FAILED %d\n" : "test_topology: ALL PASS\n", fails);
  return fails != 0;
}
