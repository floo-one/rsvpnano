#include <unity.h>
#include <FS.h>
#include "library/LibraryView.h"
#include "ui/screens/LibraryScreen.h"
#include "library/ReadingStateStore.h"
#include "storage/fs/StoragePaths.h"
#include <algorithm>
#include "article_source_tests.h"
using namespace LibraryView;
namespace LO = LibraryOrganization;
namespace Board::Storage { fs::FS& filesystem() { static fs::FS instance; return instance; } }
namespace BookLibrary { std::string_view displayName(const Entry& entry) { return entry.title; } }
void setUp() {}
void tearDown() {}
Row row(std::string name, Status status = Status::unread, size_t index = 0, bool article = false) {
    Row r;
    r.path = std::string(article ? "/library/articles/" : "/library/books/") + name + ".txt";
    r.title = name; r.source = {3, std::string(64, 'a')}; r.status = status;
    r.sourceIndex = index; r.article = article; r.availability = Availability::ready;
    return r;
}
std::vector<Row> fixture(size_t count, Status status = Status::unread) {
    std::vector<Row> rows; rows.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        char title[32]; snprintf(title, sizeof(title), "Title %04zu", i);
        rows.push_back(row(title, status, i));
    }
    return rows;
}
void defaults_and_empty_saved_tabs() {
    Model m; auto rows = fixture(4); rows[0].status = Status::reading;
    TEST_ASSERT_TRUE(m.replace(rows, {}, 1).has_value());
    TEST_ASSERT_TRUE(m.open("books")); TEST_ASSERT_TRUE(m.tab() == Tab::reading);
    m.selectTab(Tab::read); TEST_ASSERT_TRUE(m.order().empty());
    TEST_ASSERT_TRUE(m.open("needs-review")); m.open("books");
    TEST_ASSERT_TRUE(m.tab() == Tab::read);
    rows[1].status = Status::reading; TEST_ASSERT_TRUE(m.replace(rows, {}, 2).has_value());
    TEST_ASSERT_TRUE(m.tab() == Tab::read); TEST_ASSERT_TRUE(m.order().empty());
}
void filters_counts_and_shared_memberships() {
    auto rows = fixture(3); rows[1].status = Status::reading; rows[2].status = Status::unknown;
    auto a = row("Article", Status::read, 12, true); a.memberships = {{"feed-1", 1, 1}, {"feed-2", 2, 0}};
    rows.push_back(a); rows.push_back(row("Orphan", Status::unknown, 13, true));
    auto loading = row("Loading"); loading.availability = Availability::loading; rows.push_back(loading);
    TEST_ASSERT_EQUAL(1, count(rows).read); TEST_ASSERT_EQUAL(2, count(rows).unknown); TEST_ASSERT_EQUAL(1, count(rows).loading);
    TEST_ASSERT_EQUAL(1, count(rows, "feed:feed-1").read); TEST_ASSERT_EQUAL(1, count(rows, "feed:feed-2").read);
    TEST_ASSERT_EQUAL(1, project(rows, "books", Tab::unread).size());
    TEST_ASSERT_EQUAL(2, project(rows, "needs-review", Tab::unread).size());
    TEST_ASSERT_EQUAL(1, count(rows, "other-articles").unknown);
    rows[3].status = Status::reading;
    TEST_ASSERT_EQUAL(1, project(rows, "feed:feed-2", Tab::reading).size());
    TEST_ASSERT_EQUAL(1, project(rows, "feed:feed-1", Tab::reading).size());
}
void title_activity_and_arrival_sorting() {
    std::vector<Row> rows{row("z"), row("Äpfel"), row("alpha"), row("ALPHA"), row("äpple"), row("日")};
    auto order = project(rows, "books", Tab::unread);
    TEST_ASSERT_EQUAL_STRING("ALPHA", rows[order[0]].title.c_str());
    TEST_ASSERT_EQUAL_STRING("alpha", rows[order[1]].title.c_str());
    TEST_ASSERT_EQUAL_STRING("z", rows[order[2]].title.c_str());
    TEST_ASSERT_EQUAL_STRING("Äpfel", rows[order[3]].title.c_str());
    TEST_ASSERT_EQUAL_STRING("日", rows[order.back()].title.c_str());
    for (auto& r : rows) { r.article = true; r.memberships = {{"feed-1", 2, 8}}; }
    rows[0].memberships[0] = {"feed-1", 3, 2}; rows[1].memberships[0] = {"feed-1", 3, 1};
    order = project(rows, "feed:feed-1", Tab::unread); TEST_ASSERT_EQUAL(1, order[0]); TEST_ASSERT_EQUAL(0, order[1]);
    for (auto& r : rows) r.status = Status::reading;
    rows[0].lastRead = 12; rows[1].lastRead = 3;
    order = project(rows, "feed:feed-1", Tab::reading); TEST_ASSERT_EQUAL(0, order[0]); TEST_ASSERT_EQUAL(1, order[1]);
    for (auto& r : rows) r.status = Status::read;
    rows[4].completed = 100;
    order = project(rows, "feed:feed-1", Tab::read); TEST_ASSERT_EQUAL(4, order[0]);
    for (auto& r : rows) r.status = Status::unknown;
    order = project(rows, "needs-review", Tab::unread); TEST_ASSERT_EQUAL(3, order[0]);
}
void collection_order_archives_and_orphans() {
    auto rows = fixture(1); rows.push_back(row("a", Status::unread, 1, true));
    rows.back().memberships = {{"second", 0, 0}, {"lost", 0, 0}};
    LO::Feeds feeds;
    feeds.feeds = {{.id="second", .title="Second", .active=false, .subscription_order=2},
                   {.id="first", .title="First", .active=true, .subscription_order=1},
                   {.id="empty", .title="Empty", .active=false, .subscription_order=0}};
    Model m; TEST_ASSERT_TRUE(m.replace(rows, feeds, 1).has_value());
    const auto collections = m.collections(); TEST_ASSERT_EQUAL(4, collections.size());
    TEST_ASSERT_EQUAL_STRING("feed:first", collections[1].id.c_str());
    TEST_ASSERT_EQUAL_STRING("feed:second", collections[2].id.c_str()); TEST_ASSERT_FALSE(collections[2].active);
    TEST_ASSERT_EQUAL_STRING("feed:lost", collections[3].id.c_str());
    TEST_ASSERT_TRUE(m.open("feed:second")); TEST_ASSERT_TRUE(m.tab() == Tab::unread);
    TEST_ASSERT_FALSE(m.open("feed:empty"));
}
void anchors_survive_insert_reorder_filter_removal() {
    Model m; auto rows = fixture(10);
    TEST_ASSERT_TRUE(m.replace(rows, {}, 1).has_value()); m.open("books"); m.scrollTo(3 * 52 + 17);
    const auto path = m.select(3)->path;
    auto inserted = row("A first", Status::unread, 100); rows.push_back(inserted);
    TEST_ASSERT_TRUE(m.replace(rows, {}, 2).has_value());
    TEST_ASSERT_EQUAL(4 * 52 + 17, m.scroll()); TEST_ASSERT_TRUE(m.select(4)->path == path);
    rows[3].status = Status::read;
    TEST_ASSERT_TRUE(m.replace(rows, {}, 3).has_value()); TEST_ASSERT_TRUE(m.select(m.scroll()/52)->path == rows[4].path);
    TEST_ASSERT_EQUAL(0, m.scroll() % 52);
    rows.erase(rows.begin()+4); TEST_ASSERT_TRUE(m.replace(rows, {}, 4).has_value());
    TEST_ASSERT_TRUE(m.select(m.scroll()/52)->path == rows[4].path);
    m.scrollTo(INT64_MAX); TEST_ASSERT_EQUAL(static_cast<int>(m.order().size()*52 - 130), m.scroll());
}
void independent_tabs_collections_and_restart() {
    fs::FS fs; Model m; auto rows = fixture(12);
    for (size_t i=6; i<12; ++i) rows[i].status = Status::reading;
    TEST_ASSERT_TRUE(m.load(fs).has_value()); TEST_ASSERT_TRUE(m.replace(rows, {}, 1).has_value());
    m.open("books"); m.scrollTo(70); m.selectTab(Tab::unread); m.scrollTo(99);
    m.open("other-articles"); m.selectTab(Tab::read); m.open("books");
    TEST_ASSERT_EQUAL(99, m.scroll()); m.selectTab(Tab::reading); TEST_ASSERT_EQUAL(70, m.scroll());
    TEST_ASSERT_TRUE(m.save(fs).has_value()); TEST_ASSERT_FALSE(m.dirty());
    Model restarted; TEST_ASSERT_TRUE(restarted.load(fs).has_value()); TEST_ASSERT_TRUE(restarted.replace(rows, {}, 2).has_value());
    restarted.open("books"); TEST_ASSERT_TRUE(restarted.tab() == Tab::reading); TEST_ASSERT_EQUAL(70, restarted.scroll());
    restarted.selectTab(Tab::unread); TEST_ASSERT_EQUAL(99, restarted.scroll());
    restarted.open("other-articles"); TEST_ASSERT_TRUE(restarted.tab() == Tab::read);
}
void same_count_generation_and_canonical_mapping() {
    Model m; std::vector<Row> rows{row("Z", Status::unread, 19), row("A", Status::unread, 4)};
    TEST_ASSERT_TRUE(m.replace(rows, {}, 44).has_value()); m.open("books");
    auto selected = m.select(0); TEST_ASSERT_TRUE(selected.has_value());
    TEST_ASSERT_EQUAL(4, *resolve(*selected, 44, m.rows()));
    TEST_ASSERT_FALSE(resolve(*selected, 45, m.rows()).has_value());
    rows[1].source.sha256 = std::string(64, 'b'); TEST_ASSERT_TRUE(m.replace(rows, {}, 45).has_value());
    TEST_ASSERT_FALSE(resolve(*selected, 45, m.rows()).has_value());
    selected->generation = 45; TEST_ASSERT_FALSE(resolve(*selected, 45, m.rows()).has_value());
    TEST_ASSERT_FALSE(m.select(50).has_value());
}
void partial_rows_and_clamping() {
    TEST_ASSERT_EQUAL(0, clampScroll(-1, 100)); TEST_ASSERT_EQUAL(0, clampScroll(100, 2));
    TEST_ASSERT_EQUAL(26, clampScroll(100, 3)); TEST_ASSERT_EQUAL(0, clampScroll(100, 0));
    TEST_ASSERT_TRUE(fullyVisibleRow(10,0,3) == 0); TEST_ASSERT_TRUE(fullyVisibleRow(80,0,3) == 1);
    TEST_ASSERT_FALSE(fullyVisibleRow(120,0,3).has_value());
    TEST_ASSERT_FALSE(fullyVisibleRow(10,26,3).has_value()); TEST_ASSERT_TRUE(fullyVisibleRow(125,26,3) == 2);
    TEST_ASSERT_FALSE(fullyVisibleRow(130,26,3).has_value()); TEST_ASSERT_FALSE(fullyVisibleRow(0,0,0).has_value());
}
const reading::BookIdentity identity{3,42,5};
std::vector<BookLibrary::Entry> books(fs::FS& fs, size_t count) {
    std::vector<BookLibrary::Entry> result;
    for (size_t i=0; i<count; ++i) {
        auto path = "/library/books/file" + std::to_string(i) + ".txt";
        fs.put(path,"abc"); result.push_back({path,3,"Title " + std::to_string(i)});
    }
    return result;
}
auto metadata(size_t, Row&, BookMetadata&) { return std::optional{identity}; }
void legacy_status_defaults_to_unread_and_bookmarks_are_untouched() {
    fs::FS fs; auto entries = books(fs,5);
    for (size_t i=0; i<4; ++i) TEST_ASSERT_TRUE(ReadingStateStore::write(fs,entries[i].path,{identity,{.wordIndex=static_cast<uint32_t>(i == 3 ? 4 : i)}}).has_value());
    const auto writes = fs.disk->mutations.size();
    auto rows = prepare(fs,entries,metadata); TEST_ASSERT_TRUE(rows.has_value());
    TEST_ASSERT_TRUE((*rows)[0].status == Status::unread); TEST_ASSERT_TRUE((*rows)[1].status == Status::reading);
    TEST_ASSERT_TRUE((*rows)[2].status == Status::reading); TEST_ASSERT_TRUE((*rows)[3].status == Status::unread);
    TEST_ASSERT_TRUE((*rows)[4].status == Status::unread); TEST_ASSERT_EQUAL(writes, fs.disk->mutations.size());
    auto one = prepare(fs,entries,[](size_t,Row&,BookMetadata&) {return std::optional{reading::BookIdentity{3,42,1}};});
    for (size_t i = 0; i < 4; ++i) TEST_ASSERT_TRUE((*one)[i].status == Status::unknown); // Stale bookmarks.
    TEST_ASSERT_TRUE(one->back().status == Status::unread);
    // A valid one-word bookmark carries no completion history: default Unread.
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs, entries[4].path, {{3,42,1},{}}).has_value());
    one = prepare(fs,entries,[](size_t,Row&,BookMetadata&) {return std::optional{reading::BookIdentity{3,42,1}};});
    TEST_ASSERT_TRUE(one->back().status == Status::unread);
}
void legacy_items_remain_browsable_in_their_collections() {
    fs::FS fs;
    auto entries = books(fs, 6);
    for (size_t i = 0; i < 24; ++i) {
        const auto path = "/library/articles/issue" + std::to_string(i) + ".rsvp";
        fs.put(path, "@title Issue\n@source https://" + std::string(i < 20 ? "first.example" : "second.example") + "/post\n\nBody");
        entries.push_back({path, fs.bytes(path).size(), "Issue " + std::to_string(i)});
    }
    for (const auto i : {0, 6}) TEST_ASSERT_TRUE(ReadingStateStore::write(fs, entries[i].path, {identity, {.wordIndex=2}}).has_value());
    const auto writes = fs.disk->mutations.size();
    auto prepared = prepare(fs, entries, metadata); TEST_ASSERT_TRUE(prepared.has_value());
    TEST_ASSERT_EQUAL(28, count(*prepared).unread); TEST_ASSERT_EQUAL(2, count(*prepared).reading);
    TEST_ASSERT_EQUAL(0, count(*prepared, "needs-review").unknown);
    TEST_ASSERT_EQUAL(5, count(*prepared, "books").unread);
    TEST_ASSERT_EQUAL(19, count(*prepared, "source:first.example").unread);
    TEST_ASSERT_EQUAL(4, count(*prepared, "source:second.example").unread);
    TEST_ASSERT_EQUAL(0, count(*prepared, "other-articles").unread);
    Model m; TEST_ASSERT_TRUE(m.replace(*prepared, {}, 1).has_value());
    TEST_ASSERT_EQUAL(3, m.collections().size());
    TEST_ASSERT_TRUE(m.open("source:first.example")); TEST_ASSERT_TRUE(m.tab() == Tab::unread);
    TEST_ASSERT_EQUAL(19, m.order().size());
    TEST_ASSERT_TRUE(m.open("books")); TEST_ASSERT_TRUE(m.tab() == Tab::reading);
    m.selectTab(Tab::unread); TEST_ASSERT_EQUAL(5, m.order().size());
    TEST_ASSERT_EQUAL(writes, fs.disk->mutations.size());
    // The default and recovered grouping are deterministic after restart.
    auto again = prepare(fs, entries, metadata); TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_EQUAL(28, count(*again).unread);
    TEST_ASSERT_EQUAL(19, count(*again, "source:first.example").unread);
}
void legacy_defaults_preserve_errors_explicit_choices_and_feed_associations() {
    fs::FS fs; auto entries = books(fs, 6);
    LO::Item old; old.path=entries[0].path; old.source=*LO::hashSource(fs,old.path);
    old.status=Status::unknown; old.unknown_reason="legacy-unclassified";
    TEST_ASSERT_TRUE(LO::saveItem(fs,old,0).has_value());
    old.path=entries[1].path; TEST_ASSERT_TRUE(LO::saveItem(fs,old,0).has_value());
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs,old.path,{identity,{.wordIndex=2}}).has_value());
    old.path=entries[2].path; old.unknown_reason="needs-manual-review"; TEST_ASSERT_TRUE(LO::saveItem(fs,old,0).has_value());
    fs.put(StoragePaths::bookStatePathFor(entries[3].path),"broken");
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs,entries[4].path,{{999,7,5},{.wordIndex=2}}).has_value());
    const auto writes=fs.disk->mutations.size();
    auto prepared=prepare(fs,entries,metadata); TEST_ASSERT_TRUE(prepared.has_value());
    TEST_ASSERT_TRUE((*prepared)[0].status==Status::unread);
    TEST_ASSERT_TRUE((*prepared)[1].status==Status::reading);
    for (size_t i=2;i<5;++i) TEST_ASSERT_TRUE((*prepared)[i].status==Status::unknown);
    TEST_ASSERT_TRUE((*prepared)[5].status==Status::unread);
    TEST_ASSERT_EQUAL(writes,fs.disk->mutations.size());
    auto unindexed=prepare(fs,entries,{}); TEST_ASSERT_TRUE(unindexed.has_value());
    TEST_ASSERT_TRUE((*unindexed)[0].status==Status::unread);
    TEST_ASSERT_TRUE((*unindexed)[1].status==Status::unknown); // Bookmark cannot be validated without index.
    auto damaged=*ReadingStateStore::encode({identity,{.wordIndex=2}});
    const auto index=damaged.find("wordIndex = 2"); TEST_ASSERT_NOT_EQUAL(std::string::npos,index);
    damaged.replace(index,13,"wordIndex = 9999");
    fs.put(StoragePaths::bookStatePathFor(entries[5].path),damaged);
    auto invalidPosition=prepare(fs,entries,metadata); TEST_ASSERT_TRUE(invalidPosition.has_value());
    TEST_ASSERT_TRUE(invalidPosition->back().status==Status::unknown);
    TEST_ASSERT_FALSE(invalidPosition->back().progressKnown);
    TEST_ASSERT_EQUAL_STRING(damaged.c_str(),fs.bytes(StoragePaths::bookStatePathFor(entries[5].path)).c_str());
}
void publisher_groups_are_only_a_fallback_and_restore_navigation() {
    fs::FS fs; std::vector<BookLibrary::Entry> entries;
    for (size_t i=0;i<5;++i) {
        const auto path=std::string(i==4?"/library/books/":"/library/articles/")+std::to_string(i)+".rsvp";
        fs.put(path,"@title Item\n@source https://NEWS.example/post\n\nBody");
        entries.push_back({path,fs.bytes(path).size(),"Item "+std::to_string(i)});
    }
    fs.put(entries[2].path,"@source https://bad.example:123/post\n\nBody");
    fs.put(entries[3].path,"@title Item\n\n@source https://body.example/post\n");
    LO::Item explicitItem; explicitItem.path=entries[0].path; explicitItem.source=*LO::hashSource(fs,explicitItem.path);
    explicitItem.status=Status::read; explicitItem.memberships={{"confirmed",1,0}};
    TEST_ASSERT_TRUE(LO::saveItem(fs,explicitItem,0).has_value());
    auto prepared=prepare(fs,entries,metadata); TEST_ASSERT_TRUE(prepared.has_value());
    TEST_ASSERT_TRUE((*prepared)[0].publisherHost.empty());
    TEST_ASSERT_EQUAL(1, count(*prepared,"feed:confirmed").read);
    TEST_ASSERT_EQUAL(1, count(*prepared,"source:news.example").unread);
    TEST_ASSERT_EQUAL(2, count(*prepared,"other-articles").unread);
    TEST_ASSERT_EQUAL(1, count(*prepared,"books").unread);
    Model m; m.load(fs); TEST_ASSERT_TRUE(m.replace(*prepared,{},1).has_value());
    TEST_ASSERT_TRUE(m.open("source:news.example"));
    TEST_ASSERT_TRUE(m.select(0)->path==entries[1].path);
    m.selectTab(Tab::read); TEST_ASSERT_TRUE(m.save(fs).has_value());
    Model restarted; TEST_ASSERT_TRUE(restarted.load(fs).has_value()); TEST_ASSERT_TRUE(restarted.replace(*prepared,{},2).has_value());
    TEST_ASSERT_TRUE(restarted.open("source:news.example")); TEST_ASSERT_TRUE(restarted.tab()==Tab::read);
    // Removing a publisher evicts only its view; an active subscription is not needed.
    prepared->erase(prepared->begin()+1);
    TEST_ASSERT_TRUE(restarted.replace(*prepared,{},3).has_value());
    TEST_ASSERT_FALSE(restarted.open("source:news.example"));
    TEST_ASSERT_TRUE(restarted.saved().views.empty());
    LO::Views bad; bad.views={{"source:Bad.example",Tab::unread,{}}}; TEST_ASSERT_FALSE(LO::encode(bad).has_value());
}
void publisher_and_feed_collection_budget_is_shared() {
    std::vector<Row> rows;
    for (size_t i=0;i<LO::kMaxFeeds;++i) {
        auto r=row("Issue "+std::to_string(i),Status::unread,i,true);
        r.publisherHost="publisher"+std::to_string(i)+".example";
        rows.push_back(std::move(r));
    }
    Model model; TEST_ASSERT_TRUE(model.replace(rows,{},1).has_value());
    TEST_ASSERT_EQUAL(LO::kMaxFeeds+1,model.collections().size());
    LO::Feeds feeds; feeds.feeds={{.id="configured",.active=true}};
    TEST_ASSERT_FALSE(model.replace(rows,feeds,2).has_value());
    TEST_ASSERT_EQUAL(1,model.generation()); // Overflow keeps the valid snapshot.
    rows.back().publisherHost=rows.front().publisherHost;
    TEST_ASSERT_TRUE(model.replace(rows,feeds,2).has_value());
    rows.front().publisherHost="bad.example:443";
    TEST_ASSERT_FALSE(model.replace(rows,feeds,3).has_value());
}
void explicit_status_identity_and_unavailable_metadata() {
    fs::FS fs; auto entries = books(fs,4);
    LO::Item item; item.path=entries[0].path; item.source=*LO::hashSource(fs,item.path); item.status=Status::read;
    TEST_ASSERT_TRUE(LO::saveItem(fs,item,0).has_value());
    item.path=entries[1].path; item.status=Status::unread; TEST_ASSERT_TRUE(LO::saveItem(fs,item,0).has_value());
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs,item.path,{identity,{.wordIndex=2}}).has_value());
    item.path=entries[2].path; TEST_ASSERT_TRUE(LO::saveItem(fs,item,0).has_value()); fs.put(item.path,"xyz");
    fs.put(StoragePaths::libraryOrganizationPathFor(entries[3].path),"broken");
    auto rows = prepare(fs,entries,metadata); TEST_ASSERT_TRUE(rows.has_value());
    TEST_ASSERT_TRUE((*rows)[0].status == Status::read); TEST_ASSERT_TRUE((*rows)[1].status == Status::unread);
    TEST_ASSERT_TRUE((*rows)[2].status == Status::unknown); TEST_ASSERT_TRUE((*rows)[2].availability == Availability::stale);
    TEST_ASSERT_TRUE((*rows)[3].availability == Availability::corrupt);
    TEST_ASSERT_EQUAL(2, count(*rows,"needs-review").unknown);
}
void ambiguous_legacy_stems_and_missing_sources() {
    fs::FS fs; auto entries=books(fs,1); auto copy=entries[0]; copy.path="/library/books/file0.epub"; fs.put(copy.path,"abc"); entries.push_back(copy);
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs,entries[0].path,{identity,{.wordIndex=2}}).has_value());
    auto rows=prepare(fs,entries,metadata); TEST_ASSERT_TRUE(rows.has_value());
    for (auto& r:*rows) TEST_ASSERT_TRUE(r.status==Status::unknown);
    fs.remove(entries[1].path.c_str()); rows=prepare(fs,entries,metadata);
    TEST_ASSERT_TRUE((*rows)[1].availability==Availability::missing);
}
void preparation_priority_cancel_budget_and_zero_fs_projection() {
    fs::FS fs; auto entries=books(fs,3); std::vector<size_t> visited;
    auto rows=prepare(fs,entries,[&](size_t i,Row&,BookMetadata&) {visited.push_back(i); return std::optional{identity};},0,entries[2].path);
    TEST_ASSERT_TRUE(rows.has_value()); TEST_ASSERT_EQUAL(2,visited[0]); TEST_ASSERT_EQUAL(0,visited[1]); TEST_ASSERT_EQUAL(1,visited[2]);
    auto canceled=prepare(fs,entries,metadata,0,{},[](auto,auto){return false;});
    TEST_ASSERT_FALSE(canceled.has_value()); TEST_ASSERT_TRUE(canceled.error()==std::errc::operation_canceled);
    auto limited=prepare(fs,entries,metadata,kMemoryBudget); TEST_ASSERT_FALSE(limited.has_value());
    size_t io=0; fs.disk->fail=[&](const auto&,const auto&){++io; return false;};
    Model m; TEST_ASSERT_TRUE(m.replace(std::move(*rows),{},7).has_value()); m.open("needs-review"); m.scrollTo(4); m.collections(); m.select(0);
    TEST_ASSERT_EQUAL(0,io);
}
void save_failure_retry_and_future_record_protection() {
    fs::FS fs; Model m; TEST_ASSERT_TRUE(m.load(fs).has_value()); m.replace(fixture(10),{},1); m.open("books"); m.scrollTo(70);
    fs.disk->fail=[](const auto& op,const auto&){return op=="write";};
    TEST_ASSERT_FALSE(m.save(fs).has_value()); TEST_ASSERT_TRUE(m.dirty()); fs.disk->fail={};
    // An interrupted first write requires explicit recovery, never blind replacement.
    auto retry=m.save(fs); TEST_ASSERT_FALSE(retry.has_value());
    fs::FS good; Model saved; saved.load(good); saved.replace(fixture(10),{},1); saved.open("books"); TEST_ASSERT_TRUE(saved.save(good).has_value());
    saved.scrollTo(60); good.disk->fail=[](const auto& op,const auto&){return op=="write";}; TEST_ASSERT_FALSE(saved.save(good).has_value());
    good.disk->fail={}; TEST_ASSERT_TRUE(saved.save(good).has_value());
    auto bytes=good.bytes("/config/library-view.toml");
    auto at=bytes.find("rsvpnano-library v1"); TEST_ASSERT_NOT_EQUAL(std::string::npos,at); bytes.replace(at,18,"rsvpnano-library v9");
    // Future envelope versions are rejected before considering an older backup.
    good.put("/config/library-view.toml",bytes);
    Model future; TEST_ASSERT_FALSE(future.load(good).has_value()); future.replace(fixture(1),{},1); future.open("books");
    const auto before=good.bytes("/config/library-view.toml"); TEST_ASSERT_FALSE(future.save(good).has_value()); TEST_ASSERT_TRUE(before==good.bytes("/config/library-view.toml"));
}
void fixture_sizes_and_refresh_memory_budget() {
    for (size_t n : {0,1,2,3,25,100,500,1000}) {
        Model m; auto rows=fixture(n); TEST_ASSERT_TRUE(m.replace(rows,{},1).has_value()); m.open("books");
        TEST_ASSERT_EQUAL(n,m.order().size()); TEST_ASSERT_TRUE(m.memoryBytes()<kMemoryBudget);
        TEST_ASSERT_TRUE(m.replace(rows,{},2).has_value()); m.scrollTo(INT64_MAX);
        TEST_ASSERT_EQUAL(clampScroll(INT64_MAX,n),m.scroll());
    }
    Model m; auto rows=fixture(1000); for(auto& r:rows) r.title=std::string(2000,'a');
    TEST_ASSERT_FALSE(m.replace(std::move(rows),{},1).has_value());
}
void duplicate_paths_rejected_and_stale_views_evicted() {
    Model m; auto rows=fixture(1); rows.push_back(rows.front()); TEST_ASSERT_FALSE(m.replace(rows,{},1).has_value());
    LO::Feeds feeds; feeds.feeds={{.id="feed-1",.active=true}};
    TEST_ASSERT_TRUE(m.replace({},feeds,1).has_value()); TEST_ASSERT_TRUE(m.open("feed:feed-1"));
    TEST_ASSERT_TRUE(m.replace({}, {},2).has_value()); TEST_ASSERT_TRUE(m.saved().views.empty()); TEST_ASSERT_TRUE(m.collection().empty());
}
void shared_screen_prepares_off_frame_and_invalidates_same_count() {
    fs::FS fs; StorageManager storage; storage.inventory = books(fs,4);
    screens::LibraryScreen screen; IndexedBookStore store; ReadingSession session;
    TEST_ASSERT_TRUE(screen.prepare(storage,fs).has_value());
    TEST_ASSERT_EQUAL(4,storage.metadataReads);
    size_t io=0; fs.disk->fail=[&](const auto&,const auto&){++io; return false;};
    for (int i=0;i<20;++i) TEST_ASSERT_EQUAL(4,screen.items(storage,store,session).size());
    TEST_ASSERT_EQUAL(0,io); TEST_ASSERT_EQUAL(4,storage.metadataReads);
    TEST_ASSERT_FALSE(screen.selection().has_value());
    TEST_ASSERT_TRUE(screen.openCollection("books", storage));
    ui::Touch start{.actions=ui::TouchStart}; screen.observeTouch(&start);
    auto selected=screen.selection(); TEST_ASSERT_TRUE(selected.has_value());
    storage.inventory[0].path="/library/books/replaced.txt"; ++storage.revision;
    TEST_ASSERT_TRUE(screen.items(storage,store,session).empty()); TEST_ASSERT_FALSE(screen.selection().has_value());
    TEST_ASSERT_TRUE(screen.needsPreparation(storage)); TEST_ASSERT_EQUAL(0,io);
    fs.disk->fail={}; fs.put(storage.inventory[0].path,"abc");
    TEST_ASSERT_TRUE(screen.prepare(storage,fs).has_value());
    TEST_ASSERT_FALSE(resolve(*selected,storage.generation(),screen.view().rows()).has_value());
    ui::Touch release{.actions=ui::TouchRelease|ui::TouchTap}; screen.observeTouch(&release);
    TEST_ASSERT_FALSE(screen.selection().has_value()); // release from the old gesture cannot open a neighbor
    TEST_ASSERT_TRUE(screen.openCollection("needs-review",storage));
    screen.observeTouch(&start); TEST_ASSERT_FALSE(screen.selection().has_value());
    TEST_ASSERT_TRUE(screen.openCollection("books", storage));
    screen.observeTouch(&start); TEST_ASSERT_TRUE(screen.selection().has_value());
    TEST_ASSERT_EQUAL(4, screen.view().order().size());
}
void persisted_views_survive_each_interrupted_update() {
    const auto seed = [](fs::FS& fs, Model& model) {
        model.load(fs); model.replace(fixture(10),{},1); model.open("books"); model.scrollTo(60);
        TEST_ASSERT_TRUE(model.save(fs).has_value()); model.scrollTo(160); fs.disk->mutations.clear();
    };
    fs::FS probe; Model complete; seed(probe,complete); TEST_ASSERT_TRUE(complete.save(probe).has_value());
    const size_t operations=probe.disk->mutations.size();
    for (size_t cut=1;cut<=operations;++cut) {
        fs::FS fs; Model model; seed(fs,model); fs.disk->crashAfter=cut;
        try { model.save(fs); } catch (const fs::PowerLoss&) {}
        fs.reboot(); Model recovered; TEST_ASSERT_TRUE(recovered.load(fs).has_value());
        TEST_ASSERT_TRUE(recovered.replace(fixture(10),{},2).has_value()); recovered.open("books");
        TEST_ASSERT_TRUE(recovered.scroll()==60 || recovered.scroll()==160);
    }
}
void shared_article_preparation_carries_one_progress_and_status() {
    fs::FS fs; auto entries=books(fs,1); entries[0].path="/library/articles/shared.rsvp"; fs.put(entries[0].path,"abc");
    LO::Item item; item.path=entries[0].path; item.source=*LO::hashSource(fs,item.path); item.status=Status::reading;
    item.memberships={{"feed-1",4,2},{"feed-2",9,1}}; item.last_read_order=7;
    TEST_ASSERT_TRUE(LO::saveItem(fs,item,0).has_value());
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs,item.path,{identity,{.wordIndex=2}}).has_value());
    auto prepared=prepare(fs,entries,metadata); TEST_ASSERT_TRUE(prepared.has_value());
    TEST_ASSERT_EQUAL(1,prepared->size()); TEST_ASSERT_TRUE(prepared->front().progressKnown); TEST_ASSERT_EQUAL(50,prepared->front().progress);
    Model m; TEST_ASSERT_TRUE(m.replace(std::move(*prepared),{},1).has_value());
    m.open("feed:feed-1"); TEST_ASSERT_TRUE(m.order().empty()); m.selectTab(Tab::reading); const auto one=m.select(0);
    m.open("feed:feed-2"); m.selectTab(Tab::reading); TEST_ASSERT_TRUE(m.select(0)->path==one->path);
    TEST_ASSERT_EQUAL(1,count(m.rows(),"newsletters").reading);
}
void titles_and_resource_errors_are_bounded_without_truncating_items() {
    std::string longTitle; for(int i=0;i<200;++i) longTitle+="日";
    const auto title=displayTitle(longTitle,"/library/books/a.txt"); TEST_ASSERT_TRUE(title.size()<=256); TEST_ASSERT_EQUAL(0,title.size()%3);
    TEST_ASSERT_EQUAL_STRING("a.txt",displayTitle({},"/library/books/a.txt").c_str());
    fs::FS fs; auto entries=books(fs,1); entries[0].title=longTitle;
    size_t chunks=0; fs.put(entries[0].path,std::string(2048,'a'));
    auto canceled=prepare(fs,entries,metadata,0,{},[&](auto,auto){return ++chunks<3;});
    TEST_ASSERT_FALSE(canceled.has_value()); // Cancellation publishes neither partial rows nor a partial digest.
    auto rows=fixture(1); for(size_t i=0;i<=LO::kMaxFeeds;++i) rows[0].memberships.push_back({"feed-"+std::to_string(i),1,0});
    Model m; TEST_ASSERT_FALSE(m.replace(std::move(rows),{},1).has_value()); TEST_ASSERT_TRUE(m.rows().empty());
}
void browsing_context_growth_is_bounded() {
    auto rows=fixture(1); rows[0].article=true; rows[0].path="/library/articles/"+std::string(3900,'a')+".rsvp";
    for(size_t i=0;i<LO::kMaxFeeds;++i) rows[0].memberships.push_back({"feed-"+std::to_string(i),1,0});
    Model m; TEST_ASSERT_TRUE(m.replace(std::move(rows),{},1).has_value());
    bool limited=false;
    for(size_t i=0;i<LO::kMaxFeeds;++i) {
        if (!m.open("feed:feed-"+std::to_string(i))) { limited=true; break; }
    }
    TEST_ASSERT_TRUE(limited); TEST_ASSERT_TRUE(m.memoryBytes()<kMemoryBudget);
    fs::FS fs; TEST_ASSERT_FALSE(m.save(fs).has_value()); TEST_ASSERT_TRUE(fs.disk->mutations.empty());
    TEST_ASSERT_TRUE(m.open("feed:feed-0")); TEST_ASSERT_FALSE(m.persistenceError());
    m.discardRows(); TEST_ASSERT_TRUE(m.rows().empty()); TEST_ASSERT_FALSE(m.saved().views.empty());
}
int main() {
    UNITY_BEGIN();
    ArticleSourceTests::run();
    RUN_TEST(legacy_defaults_preserve_errors_explicit_choices_and_feed_associations);
    RUN_TEST(publisher_groups_are_only_a_fallback_and_restore_navigation);
    RUN_TEST(publisher_and_feed_collection_budget_is_shared);
    RUN_TEST(browsing_context_growth_is_bounded);
    RUN_TEST(persisted_views_survive_each_interrupted_update);
    RUN_TEST(shared_article_preparation_carries_one_progress_and_status);
    RUN_TEST(titles_and_resource_errors_are_bounded_without_truncating_items);
    RUN_TEST(shared_screen_prepares_off_frame_and_invalidates_same_count);
    RUN_TEST(defaults_and_empty_saved_tabs);
    RUN_TEST(legacy_items_remain_browsable_in_their_collections);
    RUN_TEST(filters_counts_and_shared_memberships);
    RUN_TEST(title_activity_and_arrival_sorting);
    RUN_TEST(collection_order_archives_and_orphans);
    RUN_TEST(anchors_survive_insert_reorder_filter_removal);
    RUN_TEST(independent_tabs_collections_and_restart);
    RUN_TEST(same_count_generation_and_canonical_mapping);
    RUN_TEST(partial_rows_and_clamping);
    RUN_TEST(legacy_status_defaults_to_unread_and_bookmarks_are_untouched);
    RUN_TEST(explicit_status_identity_and_unavailable_metadata);
    RUN_TEST(ambiguous_legacy_stems_and_missing_sources);
    RUN_TEST(preparation_priority_cancel_budget_and_zero_fs_projection);
    RUN_TEST(save_failure_retry_and_future_record_protection);
    RUN_TEST(fixture_sizes_and_refresh_memory_budget);
    RUN_TEST(duplicate_paths_rejected_and_stale_views_evicted);
    return UNITY_END();
}
