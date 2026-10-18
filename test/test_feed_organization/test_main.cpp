#include <unity.h>
#include <FS.h>
#include "feeds/FeedOrganization.h"
#include "feeds/RssConfigStorage.h"
#include "library/ReadingOrganization.h"
#include "library/ReadingStateStore.h"
#include "library/BookLibrary.h"
#include "storage/fs/StoragePaths.h"
using namespace FeedOrganization;
using namespace LibraryOrganization;
namespace Board::Storage { fs::FS& filesystem() { static fs::FS fs; return fs; } }
namespace BookLibrary { std::string_view displayName(const Entry& entry) { return entry.title; } }
void setUp() {}
void tearDown() {}
const std::vector<std::string> urls{"https://example.com/a/feed", "https://example.com/b/feed"};
Registry setup(fs::FS& fs) { auto r = reconcile(fs, urls); TEST_ASSERT_TRUE(r.has_value()); return *r; }
feedparser::FeedItem article(std::string body = "These are the words of the article.") {
    return {.title="An article", .link="https://publisher.com/article?edition=1", .author="Author", .body=body};
}
auto add(fs::FS& fs, Catalog& c, const Feed& f, const feedparser::FeedItem& item, bool permit = true, bool seen = false, bool legacy = false) {
    return ingest(fs, c, f, item, 100, 3, permit, seen, legacy);
}
auto snapshot(const fs::FS& fs) { std::map<std::string,std::string> r; for (auto& [p,n]:fs.disk->nodes) r[p]=n->bytes; return r; }
void registry_retains_ids_and_archived_collections() {
    fs::FS fs; auto r = setup(fs); const auto first = r.feeds[0].id;
    std::vector<std::string> reversed{urls[1], urls[0]}; auto next = reconcile(fs, reversed);
    TEST_ASSERT_TRUE(next.has_value()); TEST_ASSERT_EQUAL_STRING(first.c_str(), next->feeds[0].id.c_str());
    next = reconcile(fs, std::span(urls).subspan(1)); TEST_ASSERT_FALSE(next->feeds[0].active);
    TEST_ASSERT_EQUAL_UINT32(2, next->feeds.size());
    next = reconcile(fs, urls); TEST_ASSERT_TRUE(next->feeds[0].active); TEST_ASSERT_EQUAL_STRING(first.c_str(), next->feeds[0].id.c_str());
    auto changed = urls; changed[0] += "?changed=1"; next = reconcile(fs, changed);
    TEST_ASSERT_EQUAL_UINT32(3, next->feeds.size()); TEST_ASSERT_FALSE(next->feeds[0].active);
}
void fair_attempts_cover_24_feeds_even_with_failures_or_exhausted_budget() {
    fs::FS fs; std::vector<std::string> all;
    for(int i=0;i<24;++i) all.push_back("https://example.com/"+std::to_string(i));
    auto r = reconcile(fs, all); TEST_ASSERT_TRUE(r.has_value());
    std::vector<std::string> visited;
    for(int check=0;check<3;++check) {
        auto ids = attempts(*loadFeeds(fs)); TEST_ASSERT_EQUAL_UINT32(8, ids.size());
        for(const auto& id:ids) { TEST_ASSERT_TRUE(attempted(fs,id).has_value()); visited.push_back(id); }
    }
    std::ranges::sort(visited); TEST_ASSERT_TRUE(std::ranges::adjacent_find(visited)==visited.end());
    visited.clear();
    for(int check=0;check<24;++check) { const auto id=attempts(*loadFeeds(fs)).front(); TEST_ASSERT_TRUE(attempted(fs,id).has_value()); visited.push_back(id); }
    std::ranges::sort(visited); TEST_ASSERT_TRUE(std::ranges::adjacent_find(visited)==visited.end());
}
void feed_title_is_not_article_title_and_cursor_is_body_bound() {
    fs::FS fs; auto r=setup(fs);
    const std::string body="<rss><channel><title>Newsletter &amp; Notes</title><item><title>Article</title><description>Text</description></item></channel></rss>";
    auto scan=beginScan(fs,r.feeds[0].id,body); TEST_ASSERT_TRUE(scan.has_value()); TEST_ASSERT_EQUAL_STRING("Newsletter & Notes",scan->title.c_str());
    scan->scan_cursor=15; scan->scan_position=4; TEST_ASSERT_TRUE(checkpointScan(fs,*scan).has_value());
    auto again=beginScan(fs,r.feeds[0].id,body); TEST_ASSERT_EQUAL_UINT32(15,again->scan_cursor); TEST_ASSERT_EQUAL_UINT64(scan->scan_batch,again->scan_batch);
    again=beginScan(fs,r.feeds[0].id,body+" "); TEST_ASSERT_EQUAL_UINT32(0,again->scan_cursor); TEST_ASSERT_TRUE(again->scan_batch>scan->scan_batch);
    TEST_ASSERT_FALSE(checkpointScan(fs,*scan).has_value());
    TEST_ASSERT_EQUAL_STRING("",feedparser::feedTitle("<rss><channel><item><title>Only item</title></item></channel></rss>").c_str());
}
void matching_articles_share_canonical_bookmark_and_status() {
    fs::FS fs; auto r=setup(fs); Catalog c; auto a=article(); auto first=add(fs,c,r.feeds[0],a); TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_TRUE(first->outcome==Outcome::saved);
    auto source=hashSource(fs,first->path); reading::BookIdentity idx{static_cast<uint32_t>(source->bytes),42,8};
    auto t=ReadingOrganization::prepare(fs,first->path,idx,1); TEST_ASSERT_TRUE(t.has_value());
    TEST_ASSERT_TRUE(ReadingStateStore::write(fs,first->path,{idx,{.wordIndex=4}}).has_value());
    auto read=ReadingOrganization::apply(fs,{*t,"completed",ReadingOrganization::Command::markRead},*t); TEST_ASSERT_TRUE(read.has_value());
    const auto bookmark=fs.bytes(StoragePaths::bookStatePathFor(first->path));
    a.title="Syndicated title"; auto second=add(fs,c,r.feeds[1],a,false,true,true); TEST_ASSERT_TRUE(second.has_value());
    TEST_ASSERT_TRUE(second->outcome==Outcome::associated); TEST_ASSERT_EQUAL_STRING(first->path.c_str(),second->path.c_str());
    auto item=loadItem(fs,first->path,*source); TEST_ASSERT_TRUE(item->status==Status::read); TEST_ASSERT_EQUAL_UINT32(2,item->memberships.size());
    TEST_ASSERT_EQUAL_STRING(bookmark.c_str(),fs.bytes(StoragePaths::bookStatePathFor(first->path)).c_str());
    auto repeated=add(fs,c,r.feeds[1],a); TEST_ASSERT_TRUE(repeated->outcome==Outcome::unchanged);
    TEST_ASSERT_EQUAL_UINT64(item->revision,loadItem(fs,first->path,*source)->revision);
}
void uncertain_and_changed_articles_stay_separate() {
    fs::FS fs; auto r=setup(fs); Catalog c; auto a=article(); auto first=add(fs,c,r.feeds[0],a); TEST_ASSERT_TRUE(first.has_value());
    auto different=article("Different body at the same URL."); auto second=add(fs,c,r.feeds[1],different); TEST_ASSERT_TRUE(second.has_value()); TEST_ASSERT_TRUE(first->path!=second->path);
    a.link+="&tracking=1"; auto third=add(fs,c,r.feeds[1],a); TEST_ASSERT_TRUE(third.has_value()); TEST_ASSERT_TRUE(third->path!=first->path);
    a.link.clear(); auto x=add(fs,c,r.feeds[0],a); auto y=add(fs,c,r.feeds[1],a); TEST_ASSERT_TRUE(x.has_value()&&y.has_value()); TEST_ASSERT_TRUE(x->path!=y->path);
    a=article(); a.bodyComplete=false; x=add(fs,c,r.feeds[0],a); y=add(fs,c,r.feeds[1],a); TEST_ASSERT_TRUE(x.has_value()&&y.has_value()); TEST_ASSERT_TRUE(x->path!=y->path);
}
void replaced_content_cannot_inherit_membership() {
    fs::FS fs; auto r=setup(fs); Catalog c; auto a=article(); auto first=add(fs,c,r.feeds[0],a); TEST_ASSERT_TRUE(first.has_value());
    fs.put(first->path,"replacement"); const auto oldMetadata=fs.bytes(StoragePaths::libraryOrganizationPathFor(first->path));
    auto next=add(fs,c,r.feeds[1],a); TEST_ASSERT_TRUE(next.has_value()); TEST_ASSERT_TRUE(next->path!=first->path);
    TEST_ASSERT_EQUAL_STRING("replacement",fs.bytes(first->path).c_str());
    TEST_ASSERT_EQUAL_STRING(oldMetadata.c_str(),fs.bytes(StoragePaths::libraryOrganizationPathFor(first->path)).c_str());
}
void collision_and_orphan_bookmarks_are_not_overwritten() {
    fs::FS fs; auto r=setup(fs); Catalog c; auto a=article();
    const auto path=std::string(StoragePaths::kArticleFilesPrefix)+"article-"+seenIdentity(r.feeds[0],a)+".rsvp";
    fs.put(path,"unrelated existing content"); fs.put(StoragePaths::bookStatePathFor(path),"old bookmark");
    auto saved=add(fs,c,r.feeds[0],a); TEST_ASSERT_TRUE(saved.has_value()); TEST_ASSERT_TRUE(saved->path!=path);
    TEST_ASSERT_EQUAL_STRING("unrelated existing content",fs.bytes(path).c_str()); TEST_ASSERT_EQUAL_STRING("old bookmark",fs.bytes(StoragePaths::bookStatePathFor(path)).c_str());
}
void deleted_seen_items_do_not_resurrect() {
    fs::FS fs; auto r=setup(fs); Catalog c; auto a=article(); auto saved=add(fs,c,r.feeds[0],a); TEST_ASSERT_TRUE(saved.has_value());
    fs.disk->nodes.erase(saved->path); auto before=snapshot(fs);
    TEST_ASSERT_TRUE(recover(fs).has_value()); TEST_ASSERT_TRUE(before==snapshot(fs));
    c.clear(); auto next=add(fs,c,r.feeds[0],a,true,true,false); TEST_ASSERT_TRUE(next->outcome==Outcome::skipped);
    next=add(fs,c,r.feeds[0],a,true,false,true); TEST_ASSERT_TRUE(next->outcome==Outcome::skipped);
    TEST_ASSERT_TRUE(before==snapshot(fs));
}
void import_interruptions_recover_without_touching_other_bookmarks() {
    size_t boundaries=0;
    for(size_t crash=0;crash<=boundaries;++crash) {
        fs::FS fs; auto r=setup(fs); Catalog c; fs.put("/library/articles/.other.rstate.toml","bookmark"); fs.reboot(); fs.disk->crashAfter=crash;
        try { TEST_ASSERT_TRUE(add(fs,c,r.feeds[0],article()).has_value()); } catch(const fs::PowerLoss&) {}
        if(crash==0) boundaries=fs.disk->mutations.size();
        fs.reboot(); auto recovered=recover(fs); TEST_ASSERT_TRUE_MESSAGE(recovered.has_value(),std::to_string(crash).c_str());
        auto before=snapshot(fs); TEST_ASSERT_TRUE(recover(fs).has_value()); TEST_ASSERT_TRUE(before==snapshot(fs));
        TEST_ASSERT_EQUAL_STRING("bookmark",fs.bytes("/library/articles/.other.rstate.toml").c_str());
        // Retrying after recovery rediscovers any committed import by catalog.
        std::vector<std::string> paths; for(auto&[p,n]:fs.disk->nodes) if(p.ends_with(".rsvp"))paths.push_back(p);
        auto rebuilt=catalog(fs,paths); TEST_ASSERT_TRUE(rebuilt.has_value());
        TEST_ASSERT_TRUE(add(fs,*rebuilt,r.feeds[0],article()).has_value());
        size_t content=0; for(auto&[p,n]:fs.disk->nodes) if(p.ends_with(".rsvp"))++content;
        TEST_ASSERT_EQUAL_UINT32(1,content);
    }
    TEST_ASSERT_GREATER_THAN(15,boundaries);
}
void failed_import_does_not_mark_seen_and_can_retry() {
    fs::FS fs; auto r=setup(fs); Catalog c;
    std::string body="<rss><channel><item><title>Title</title><link>https://x.test/a</link><description>Content here</description></item></channel></rss>";
    auto f=beginScan(fs,r.feeds[0].id,body); TEST_ASSERT_TRUE(f.has_value()); size_t marks=0;
    fs.disk->fail=[](const auto& op,const auto& path){return op=="open-write" && path==std::string(kImportPath)+".tmp";};
    auto result=scan(fs,c,*f,body,12,{},[&](auto){++marks;}); TEST_ASSERT_TRUE(result.has_value()); TEST_ASSERT_EQUAL_UINT32(1,result->failed); TEST_ASSERT_EQUAL_UINT32(0,marks);
    fs.reboot(); f=beginScan(fs,r.feeds[0].id,body); result=scan(fs,c,*f,body,12,{},[&](auto){++marks;}); TEST_ASSERT_TRUE(result.has_value()); TEST_ASSERT_EQUAL_UINT32(1,result->saved); TEST_ASSERT_EQUAL_UINT32(1,marks);
}
void bounded_scan_advances_and_associations_ignore_save_budget() {
    fs::FS fs; auto r=setup(fs); Catalog c; auto a=article(); TEST_ASSERT_TRUE(add(fs,c,r.feeds[0],a).has_value());
    std::string body="<rss><channel>";
    for(int i=0;i<140;++i)body+="<item><title>Item</title><link>https://publisher.com/article?edition=1</link><description>These are the words of the article.</description></item>";
    body+="</channel></rss>";
    auto f=beginScan(fs,r.feeds[1].id,body); auto result=scan(fs,c,*f,body,0,{},{}); TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL_UINT32(128,result->scanned); TEST_ASSERT_EQUAL_UINT32(1,result->associated); TEST_ASSERT_EQUAL_UINT32(0,result->saved);
    f=beginScan(fs,r.feeds[1].id,body); TEST_ASSERT_TRUE(f->scan_cursor>0);
    result=scan(fs,c,*f,body,0,{},{}); TEST_ASSERT_EQUAL_UINT32(12,result->scanned);
    f=beginScan(fs,r.feeds[1].id,body); TEST_ASSERT_EQUAL_UINT32(0,f->scan_cursor);
}
void url_resolution_preserves_queries_and_rejects_opaque_ids() {
    TEST_ASSERT_EQUAL_STRING("https://example.com/item?q=1",articleUrl(urls[0],"/item?q=1").c_str());
    TEST_ASSERT_EQUAL_STRING("https://example.com/a/item",articleUrl(urls[0],"item").c_str());
    TEST_ASSERT_EQUAL_STRING("https://other.test/x",articleUrl(urls[0],"//other.test/x").c_str());
    TEST_ASSERT_TRUE(articleUrl(urls[0],"urn:uuid:xyz").empty()); TEST_ASSERT_TRUE(articleUrl(urls[0],"javascript:alert(1)").empty());
    feedparser::FeedItem item; size_t cursor=0;
    TEST_ASSERT_TRUE(feedparser::parseNextItem("<item><title>T</title><guid>opaque-id</guid><description>body</description></item>",cursor,item)); TEST_ASSERT_TRUE(item.link.empty());
}
void exact_legacy_match_preserves_bookmark_and_unknown_status() {
    fs::FS fs; auto r=setup(fs); Catalog c; auto a=article();
    auto initial=add(fs,c,r.feeds[0],a); TEST_ASSERT_TRUE(initial.has_value());
    const auto legacy=std::string(StoragePaths::kArticleFilesPrefix)+legacyFilename(a);
    fs.put(legacy,fs.bytes(initial->path)); fs.disk->nodes.erase(initial->path);
    fs.disk->nodes.erase(StoragePaths::libraryOrganizationPathFor(initial->path)); c.clear();
    fs.put(StoragePaths::bookStatePathFor(legacy),"legacy bookmark, untouched");
    auto matched=add(fs,c,r.feeds[0],a,false,false,true); TEST_ASSERT_TRUE(matched.has_value());
    TEST_ASSERT_EQUAL_STRING(legacy.c_str(),matched->path.c_str());
    auto item=inspectItem(fs,legacy); TEST_ASSERT_TRUE(item.has_value()); TEST_ASSERT_TRUE(item->status==Status::unknown);
    a.title="Syndicated title differs"; auto other=add(fs,c,r.feeds[1],a,false,false,true); TEST_ASSERT_TRUE(other.has_value());
    TEST_ASSERT_EQUAL_STRING(legacy.c_str(),other->path.c_str());
    TEST_ASSERT_EQUAL_STRING("legacy bookmark, untouched",fs.bytes(StoragePaths::bookStatePathFor(legacy)).c_str());
}
void existing_legacy_status_survives_identity_backfill() {
    fs::FS fs; auto r=setup(fs); Catalog c; auto a=article();
    auto initial=add(fs,c,r.feeds[0],a); TEST_ASSERT_TRUE(initial.has_value());
    const auto legacy=std::string(StoragePaths::kArticleFilesPrefix)+legacyFilename(a);
    fs.put(legacy,fs.bytes(initial->path)); fs.disk->nodes.erase(initial->path); c.clear();
    auto source=hashSource(fs,legacy); Item item; item.path=legacy; item.source=*source; item.status=Status::read; item.completed_order=50;
    TEST_ASSERT_TRUE(saveItem(fs,item,0).has_value());
    auto matched=add(fs,c,r.feeds[0],a); TEST_ASSERT_TRUE(matched.has_value());
    auto updated=inspectItem(fs,legacy); TEST_ASSERT_TRUE(updated->status==Status::read); TEST_ASSERT_EQUAL_UINT64(50,updated->completed_order);
    TEST_ASSERT_FALSE(updated->body_sha256.empty());
}
void all_import_io_failures_preserve_existing_content_and_allow_recovery() {
    size_t count=0;
    for(size_t failAt=0;failAt<=count;++failAt) {
        fs::FS fs; auto r=setup(fs); Catalog c; auto original=add(fs,c,r.feeds[0],article()); TEST_ASSERT_TRUE(original.has_value());
        const auto bytes=fs.bytes(original->path); auto next=article("Another article body.");
        fs.reboot(); size_t calls=0; fs.disk->fail=[&](const auto&,const auto&){ return ++calls==failAt; };
        auto result=add(fs,c,r.feeds[1],next); if(failAt==0){count=calls;TEST_ASSERT_TRUE(result.has_value());}
        fs.reboot(); auto recovered=recover(fs); TEST_ASSERT_TRUE_MESSAGE(recovered.has_value(),std::to_string(failAt).c_str());
        TEST_ASSERT_EQUAL_STRING(bytes.c_str(),fs.bytes(original->path).c_str());
        std::vector<std::string> paths; for(auto&[p,n]:fs.disk->nodes) if(p.ends_with(".rsvp"))paths.push_back(p);
        auto rebuilt=catalog(fs,paths); TEST_ASSERT_TRUE(rebuilt.has_value());
        TEST_ASSERT_TRUE_MESSAGE(add(fs,*rebuilt,r.feeds[1],next).has_value(),std::to_string(failAt).c_str());
        size_t contents=0;for(auto&[p,n]:fs.disk->nodes)if(p.ends_with(".rsvp"))++contents;
        TEST_ASSERT_EQUAL_UINT32(2,contents);
    }
    TEST_ASSERT_GREATER_THAN(30,count);
}
void subsequent_import_power_loss_does_not_replay_previous_receipt() {
    size_t boundaries=0;
    for(size_t crash=0;crash<=boundaries;++crash) {
        fs::FS fs; auto r=setup(fs); Catalog c; auto first=add(fs,c,r.feeds[0],article());TEST_ASSERT_TRUE(first.has_value());
        const auto original=fs.bytes(first->path); fs.reboot(); fs.disk->crashAfter=crash;
        auto next=article("The next article content.");
        try {TEST_ASSERT_TRUE(add(fs,c,r.feeds[1],next).has_value());}catch(const fs::PowerLoss&){}
        if(crash==0)boundaries=fs.disk->mutations.size();
        fs.reboot();TEST_ASSERT_TRUE_MESSAGE(recover(fs).has_value(),std::to_string(crash).c_str());
        std::vector<std::string> paths;for(auto&[p,n]:fs.disk->nodes)if(p.ends_with(".rsvp"))paths.push_back(p);
        auto rebuilt=catalog(fs,paths);TEST_ASSERT_TRUE(rebuilt.has_value());
        TEST_ASSERT_TRUE(add(fs,*rebuilt,r.feeds[1],next).has_value());
        TEST_ASSERT_EQUAL_STRING(original.c_str(),fs.bytes(first->path).c_str());
        size_t contents=0;for(auto&[p,n]:fs.disk->nodes)if(p.ends_with(".rsvp"))++contents;TEST_ASSERT_EQUAL_UINT32(2,contents);
    }
}
void membership_interruption_preserves_progress_and_retries() {
    size_t boundaries=0;
    for(size_t crash=0;crash<=boundaries;++crash) {
        fs::FS fs;auto r=setup(fs);Catalog c;auto first=add(fs,c,r.feeds[0],article());TEST_ASSERT_TRUE(first.has_value());
        fs.put(StoragePaths::bookStatePathFor(first->path),"bookmark");fs.reboot();fs.disk->crashAfter=crash;
        try {TEST_ASSERT_TRUE(add(fs,c,r.feeds[1],article(),false).has_value());}catch(const fs::PowerLoss&){}
        if(crash==0)boundaries=fs.disk->mutations.size();
        fs.reboot();TEST_ASSERT_TRUE(add(fs,c,r.feeds[1],article(),false).has_value());
        TEST_ASSERT_EQUAL_UINT32(2,inspectItem(fs,first->path)->memberships.size());
        TEST_ASSERT_EQUAL_STRING("bookmark",fs.bytes(StoragePaths::bookStatePathFor(first->path)).c_str());
    }
}
void config_save_updates_registry_without_changing_url_payload() {
    fs::FS fs;Board::Storage::filesystem()=fs;rss::Config config{urls};
    TEST_ASSERT_TRUE(rss::save(fs,config).has_value()); TEST_ASSERT_TRUE(rss::load(fs)->feeds==urls);
    const auto id=loadFeeds(fs)->feeds[0].id;
    config.feeds={urls[1]};TEST_ASSERT_TRUE(rss::save(fs,config).has_value());
    auto registry=loadFeeds(fs); TEST_ASSERT_FALSE(registry->feeds[0].active);TEST_ASSERT_EQUAL_STRING(id.c_str(),registry->feeds[0].id.c_str());
    TEST_ASSERT_TRUE(rss::load(fs)->feeds==config.feeds);
}
void import_codec_rejects_future_and_invalid_receipts() {
    fs::FS fs;auto r=setup(fs);Catalog c;TEST_ASSERT_TRUE(add(fs,c,r.feeds[0],article()).has_value());
    auto intent=loadImport(fs);TEST_ASSERT_TRUE(intent.has_value());auto bytes=encode(*intent);TEST_ASSERT_TRUE(bytes.has_value());
    TEST_ASSERT_TRUE(decodeImport(*bytes)==intent);TEST_ASSERT_FALSE(decodeImport(bytes->substr(0,bytes->size()-3)).has_value());
    intent->item.path="/library/books/book.rsvp";TEST_ASSERT_FALSE(encode(*intent).has_value());
    intent=loadImport(fs);intent->schema_version=2;TEST_ASSERT_FALSE(encode(*intent).has_value());
}
void registry_initial_creation_recovers_from_every_interruption() {
    size_t boundaries=0;
    for(size_t crash=0;crash<=boundaries;++crash){
        fs::FS fs;fs.disk->crashAfter=crash;
        try{TEST_ASSERT_TRUE(reconcile(fs,urls).has_value());}catch(const fs::PowerLoss&){}
        if(crash==0)boundaries=fs.disk->mutations.size();
        fs.reboot();auto r=reconcile(fs,urls);TEST_ASSERT_TRUE_MESSAGE(r.has_value(),std::to_string(crash).c_str());
        TEST_ASSERT_EQUAL_UINT32(2,r->feeds.size());
    }
}
void saved_arrival_order_is_included_in_counter_reconstruction() {
    fs::FS fs;auto r=setup(fs);Catalog c;auto first=add(fs,c,r.feeds[0],article());TEST_ASSERT_TRUE(first.has_value());
    const std::string_view paths[]{first->path};TEST_ASSERT_TRUE(ReadingOrganization::reconcileOrder(fs,paths).has_value());
    TEST_ASSERT_TRUE(loadFeeds(fs)->next_order>100);
}
void atom_alternate_and_opaque_guids_are_conservative() {
    const std::string body="<feed><title>Channel</title><entry><title>A</title><link rel=\"self\" href=\"https://api.test/entry\"/><link rel=\"alternate\" href=\"/article\"/><content>Text</content></entry></feed>";
    size_t at=0;feedparser::FeedItem item;TEST_ASSERT_TRUE(feedparser::parseNextItem(body,at,item));
    TEST_ASSERT_EQUAL_STRING("https://api.test/entry",item.legacyIdentity.c_str());
    TEST_ASSERT_EQUAL_STRING("/article",item.link.c_str());TEST_ASSERT_EQUAL_STRING("Channel",feedparser::feedTitle(body).c_str());
    at=0;TEST_ASSERT_TRUE(feedparser::parseNextItem("<item><guid isPermaLink=\"false\">https://opaque.test/id</guid><description>body</description></item>",at,item));TEST_ASSERT_TRUE(item.link.empty());
    TEST_ASSERT_EQUAL_STRING("https://opaque.test/id",item.legacyIdentity.c_str());
    at=0;TEST_ASSERT_TRUE(feedparser::parseNextItem("<feed xml:base=\"https://other.test/\"><entry><link href=\"relative\"/><content>body</content></entry></feed>",at,item));TEST_ASSERT_FALSE(item.bodyComplete);
}
void budgets_defer_without_losing_items_and_never_mark_deferred_seen() {
    fs::FS fs;auto r=setup(fs);Catalog c;std::string body="<rss><channel>";
    for(int i=0;i<15;++i)body+="<item><title>T</title><link>https://x.test/"+std::to_string(i)+"</link><description>Article words</description></item>";
    body+="</channel></rss>";size_t marks=0;auto feed=beginScan(fs,r.feeds[0].id,body);
    auto result=scan(fs,c,*feed,body,12,{},[&](auto){++marks;});TEST_ASSERT_TRUE(result.has_value());TEST_ASSERT_EQUAL_UINT32(12,result->saved);TEST_ASSERT_EQUAL_UINT32(3,result->deferred);TEST_ASSERT_EQUAL_UINT32(12,marks);
    feed=beginScan(fs,r.feeds[0].id,body);result=scan(fs,c,*feed,body,12,{},{});TEST_ASSERT_TRUE(result.has_value());TEST_ASSERT_EQUAL_UINT32(3,result->saved);TEST_ASSERT_EQUAL_UINT32(15,c.size());
}
void limits_fail_explicitly_and_pending_restart_blocks_association() {
    fs::FS fs;auto r=setup(fs);Catalog c;auto saved=add(fs,c,r.feeds[0],article());TEST_ASSERT_TRUE(saved.has_value());
    auto item=inspectItem(fs,saved->path);item->restart=RestartIntent{"pending",false,"",0,100};TEST_ASSERT_TRUE(saveItem(fs,*item,item->revision).has_value());
    const auto before=snapshot(fs);TEST_ASSERT_FALSE(add(fs,c,r.feeds[1],article(),false).has_value());TEST_ASSERT_TRUE(before==snapshot(fs));
    std::vector<std::string> tooMany;for(int i=0;i<25;++i)tooMany.push_back("https://x.test/"+std::to_string(i));TEST_ASSERT_FALSE(reconcile(fs,tooMany).has_value());
    TEST_ASSERT_TRUE(before==snapshot(fs));
    item=inspectItem(fs,saved->path);item->restart.reset();TEST_ASSERT_TRUE(saveItem(fs,*item,item->revision).has_value());
    c.clear();Candidate huge;huge.path=std::string(kCatalogBytes,'x');c.push_back(std::move(huge));
    auto different=article("Different text");TEST_ASSERT_FALSE(add(fs,c,r.feeds[1],different).has_value());
}
void orphan_backup_bookmark_does_not_get_a_new_article() {
    fs::FS fs;auto r=setup(fs);Catalog c;auto a=article();
    const auto path=std::string(StoragePaths::kArticleFilesPrefix)+"article-"+seenIdentity(r.feeds[0],a)+".rsvp";
    const auto bookmark=StoragePaths::bookStatePathFor(path)+".bak";fs.put(bookmark,"precious bookmark");
    auto saved=add(fs,c,r.feeds[0],a);TEST_ASSERT_TRUE(saved.has_value());TEST_ASSERT_TRUE(saved->path!=path);
    TEST_ASSERT_EQUAL_STRING("precious bookmark",fs.bytes(bookmark).c_str());
}
void legacy_first_membership_recovers_at_each_write_boundary() {
    size_t boundaries=0;
    for(size_t crash=0;crash<=boundaries;++crash) {
        fs::FS fs;auto r=setup(fs);Catalog c;auto a=article();auto generated=add(fs,c,r.feeds[0],a);TEST_ASSERT_TRUE(generated.has_value());
        const auto legacy=std::string(StoragePaths::kArticleFilesPrefix)+legacyFilename(a);
        fs.put(legacy,fs.bytes(generated->path));fs.disk->nodes.erase(generated->path);c.clear();
        fs.put(StoragePaths::bookStatePathFor(legacy),"prior position");fs.reboot();fs.disk->crashAfter=crash;
        try {TEST_ASSERT_TRUE(add(fs,c,r.feeds[0],a).has_value());}catch(const fs::PowerLoss&){}
        if(crash==0)boundaries=fs.disk->mutations.size();
        fs.reboot();TEST_ASSERT_TRUE_MESSAGE(recover(fs).has_value(),std::to_string(crash).c_str());
        std::vector<std::string> paths{legacy};auto rebuilt=catalog(fs,paths);TEST_ASSERT_TRUE(rebuilt.has_value());
        TEST_ASSERT_TRUE(add(fs,*rebuilt,r.feeds[0],a).has_value());
        auto item=inspectItem(fs,legacy);TEST_ASSERT_TRUE(item.has_value());TEST_ASSERT_TRUE(item->status==Status::unknown);
        TEST_ASSERT_EQUAL_UINT32(1,item->memberships.size());
        TEST_ASSERT_EQUAL_STRING("prior position",fs.bytes(StoragePaths::bookStatePathFor(legacy)).c_str());
    }
}
void committed_import_recovery_does_not_reset_new_reading_status() {
    fs::FS fs;auto r=setup(fs);Catalog c;size_t writes=0;
    fs.disk->fail=[&](const auto& op,const auto& path){return op=="open-write" && path==std::string(kImportPath)+".tmp" && ++writes==2;};
    TEST_ASSERT_FALSE(add(fs,c,r.feeds[0],article()).has_value());fs.reboot();
    auto intent=loadImport(fs);TEST_ASSERT_TRUE(intent.has_value());TEST_ASSERT_FALSE(intent->completed);
    auto item=inspectItem(fs,intent->item.path);TEST_ASSERT_TRUE(item.has_value());item->status=Status::read;item->completed_order=123;
    TEST_ASSERT_TRUE(saveItem(fs,*item,item->revision).has_value());
    TEST_ASSERT_TRUE(recover(fs).has_value());item=inspectItem(fs,intent->item.path);
    TEST_ASSERT_TRUE(item->status==Status::read);TEST_ASSERT_EQUAL_UINT64(123,item->completed_order);
}
int main(){UNITY_BEGIN();
RUN_TEST(registry_retains_ids_and_archived_collections);
RUN_TEST(fair_attempts_cover_24_feeds_even_with_failures_or_exhausted_budget);
RUN_TEST(feed_title_is_not_article_title_and_cursor_is_body_bound);
RUN_TEST(matching_articles_share_canonical_bookmark_and_status);
RUN_TEST(uncertain_and_changed_articles_stay_separate);
RUN_TEST(replaced_content_cannot_inherit_membership);
RUN_TEST(collision_and_orphan_bookmarks_are_not_overwritten);
RUN_TEST(deleted_seen_items_do_not_resurrect);
RUN_TEST(import_interruptions_recover_without_touching_other_bookmarks);
RUN_TEST(failed_import_does_not_mark_seen_and_can_retry);
RUN_TEST(bounded_scan_advances_and_associations_ignore_save_budget);
RUN_TEST(url_resolution_preserves_queries_and_rejects_opaque_ids);
RUN_TEST(exact_legacy_match_preserves_bookmark_and_unknown_status);
RUN_TEST(existing_legacy_status_survives_identity_backfill);
RUN_TEST(all_import_io_failures_preserve_existing_content_and_allow_recovery);
RUN_TEST(subsequent_import_power_loss_does_not_replay_previous_receipt);
RUN_TEST(membership_interruption_preserves_progress_and_retries);
RUN_TEST(config_save_updates_registry_without_changing_url_payload);
RUN_TEST(import_codec_rejects_future_and_invalid_receipts);
RUN_TEST(registry_initial_creation_recovers_from_every_interruption);
RUN_TEST(saved_arrival_order_is_included_in_counter_reconstruction);
RUN_TEST(atom_alternate_and_opaque_guids_are_conservative);
RUN_TEST(budgets_defer_without_losing_items_and_never_mark_deferred_seen);
RUN_TEST(limits_fail_explicitly_and_pending_restart_blocks_association);
RUN_TEST(orphan_backup_bookmark_does_not_get_a_new_article);
RUN_TEST(legacy_first_membership_recovers_at_each_write_boundary);
RUN_TEST(committed_import_recovery_does_not_reset_new_reading_status);
return UNITY_END();}
