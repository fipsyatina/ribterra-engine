// Included by persistence_tests.cpp inside its test namespace.
std::vector<std::uint8_t> canonical_v6_fields(const std::vector<std::uint8_t> &file) {
    if (file.size() < 96) { CHECK(false); return {}; }
    std::uint64_t size = 0;
    for (std::size_t i = 0; i < 8; ++i)
        size |= static_cast<std::uint64_t>(file[24 + i]) << (i * 8U);
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(size));
    const auto decoded = ZSTD_decompress(payload.data(), payload.size(), file.data() + 96, file.size() - 96);
    CHECK(!ZSTD_isError(decoded) && decoded == payload.size());
    if (ZSTD_isError(decoded) || decoded != payload.size()) return {};
    flatbuffers::Verifier verifier(payload.data(), payload.size(), 64, 2'000'000);
    const bool verified = ribterra::save::VerifyWorldSaveBuffer(verifier);
    CHECK(verified);
    if (!verified) return {};
    std::unique_ptr<ribterra::save::WorldSaveT> object(ribterra::save::GetWorldSave(payload.data())->UnPack());
    object->settlement.reset(); // Added after v6; compare only historical fields.
    object->manual_orders.clear();
    object->next_manual_order_id = 1;
    object->manual_order_cursor_id = 1;
    for (auto &batch : object->path_batches) {
        batch->legacy_atomic = true;
        batch->legacy_grid = false;
        for (auto &draft : batch->drafts) {
            draft->manual_order_id = 0;
            draft->resolved = false;
            draft->failure = ribterra::save::PathFailure::None;
        }
    }
    object->profession_cursor_id = 1;
    for (auto &resident : object->residents) {
        resident->additional_xp.clear();
        resident->skill_practice.clear();
        resident->abilities_bp.clear();
        resident->profession.reset();
    }
    std::erase_if(object->chunks, [](const auto &c) { return c->cx > 4 || c->cy > 4; });
    // Repack both sides with the same generated object writer. This compares every
    // pre-profession field, including full planner nodes/drafts, without relying on
    // the original writer's allocation order or the new gameplay checksum shape.
    flatbuffers::FlatBufferBuilder builder;
    ribterra::save::FinishWorldSaveBuffer(builder, ribterra::save::WorldSave::Pack(builder, object.get()));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

void captured_v6_states_preserve_every_legacy_field() {
    using namespace ribterra;
    struct Fixture { const char *name; std::size_t bytes; const char *sha; std::uint64_t tick; };
    constexpr std::array<Fixture, 8> fixtures{{
        {"carry", 521834, "17e6f764fd00692cbb3927c18f50adfd676a63c8a9958dbc3d94986ee6fee323", 18},
        {"waiting", 525179, "307d9d9cece3fd83dbfe85f6c46eaf68cf3048b1614b682b1ba4726131a1d198", 681},
        {"builder_planning", 522610, "7f649fceab3e6eeb83cb66d649ed939c8319aa09a784ce86d599a232cb9c325b", 253},
        {"builder_travelling", 522656, "1308cbe37b21365084d0f02ab15b2d3f5d8d25ae3e21f743d0a6fa17509c9157", 254},
        {"builder_working", 522717, "7fbce09a2d26f10da2bde64b4cfc6155820755ede338319eb109f27adf6d49da", 469},
        {"exhausted", 521681, "bf30a1948e3bd6bd14b4f5e274e236df7d14bb1668ccc3a40d8a5b060c2782df", 1720},
        {"failed", 525200, "df3fe627c59421faf47a54c1f011c44f98c7a3499a687e0d3419bf7972d88e43", 1880},
        {"work_search", 574228, "c2b765316351c70a70f0fca6e9b20f8fd22e0e642e3595ca0f41583f7bc5bb97", 1551}
    }};
    for (const auto &fixture : fixtures) {
        const std::string name = std::string("world_save_v6_") + fixture.name;
        const auto original = read_bytes(std::filesystem::path(__FILE__).parent_path() / "fixtures" / (name + ".ribsave"));
        CHECK(original.size() == fixture.bytes && sha256(original) == fixture.sha);
        TempSave slot("ribterra_migrate_" + name + ".ribsave");
        write_bytes(slot.path, original);
        World migrated(1, 1), restored(2, 2);
        std::string error;
        const bool loaded = migrated.load(slot.path, error);
        CHECK(loaded);
        if (!loaded) { std::cerr << name << ": " << error << '\n'; continue; }
        const auto upgraded = read_bytes(slot.path);
        CHECK(upgraded.size() >= 20 && upgraded[12] == 8 && upgraded[16] == rules_version);
        CHECK(read_bytes(slot.path.string() + ".v6.bak") == original);
        const auto legacy_fields = canonical_v6_fields(original);
        CHECK(!legacy_fields.empty() && canonical_v6_fields(upgraded) == legacy_fields);
        const auto state = migrated.export_state();
        CHECK(state.ticks == fixture.tick && state.profession_cursor_id == 1);
        for (const auto &resident : state.residents) {
            CHECK((resident.additional_xp == std::array<int, skill_count - 3>{}));
            CHECK((resident.abilities_bp == std::array<std::int16_t, skill_count>{}));
            CHECK(resident.profession == ProfessionState{});
            for (const auto &practice : resident.skill_practice)
                CHECK((practice == SkillPractice{0, 0, fixture.tick}));
        }
        if (std::string_view(fixture.name) == "work_search") {
            CHECK(state.path_batches.size() == 1);
            if (!state.path_batches.empty()) {
                const auto &batch = state.path_batches.front();
                CHECK(batch.command == PathCommandKind::Work && batch.phase == PathPlannerPhase::Search);
                CHECK(batch.work_units_done == 4096 && batch.nodes.size() == 4334);
            }
        }
        CHECK(restored.load(slot.path, error));
        CHECK(migrated.checksum() == restored.checksum());
        for (int tick = 0; tick < 128; ++tick) {
            if (tick == 32) CHECK(migrated.take_path_results() == restored.take_path_results());
            migrated.tick(); restored.tick();
            CHECK(migrated.checksum() == restored.checksum());
        }
        if (std::string_view(fixture.name) == "failed") {
            CHECK(!migrated.work_intents().empty());
            if (!migrated.work_intents().empty()) CHECK(std::any_of(migrated.work_intents().begin(),migrated.work_intents().end(),[&](const auto &intent){return intent.intent_id==state.work_intents.back().intent_id && intent.phase==WorkIntentPhase::Failed;}));
        }
        if (std::string_view(fixture.name) == "exhausted") {
            CHECK(!migrated.work_intents().empty());
            if (!migrated.work_intents().empty()) CHECK(migrated.work_intents().back().completion == WorkCompletionReason::Exhausted);
        }
    }
}

void profession_and_skill_state_roundtrip() {
    using namespace ribterra;
    World world(70421, 2);
    auto state = world.export_state();
    state.ticks = 15000;
    state.profession_cursor_id = 2;
    for (auto &resident : state.residents) {
        resident.wood_xp = 123; resident.mining_xp = 456; resident.farming_xp = 789;
        resident.additional_xp = {100, 200, 300, 400, 500};
        resident.abilities_bp = {-1500, -1000, -10, 0, 10, 1000, 1499, 1500};
        for (std::size_t i = 0; i < skill_count; ++i)
            resident.skill_practice[i] = {static_cast<std::uint32_t>(10 + i),
                                          static_cast<std::uint32_t>(100 + i), state.ticks - i};
    }
    std::string error;
    CHECK(world.import_state(std::move(state), error));
    ProfessionAssignment assignment;
    assignment.profession = Profession::Woodcutter;
    assignment.worksite = CellCoord{22, 19, WorldLayer::Surface};
    assignment.priorities = {5, 0, 1, 0, 2};
    const auto assigned = world.assign_profession({1}, assignment);
    CHECK(assigned.size() == 1 && assigned.front().status == ResidentCommandStatus::Applied);
    CHECK(world.move({1}, 26, 24).accepted);
    const auto before = world.export_state();
    CHECK(before.residents.front().profession.manual_override);
    TempSave slot("ribterra_profession_roundtrip.ribsave");
    CHECK(world.save(slot.path, error));
    World copy(1, 0);
    const bool loaded = copy.load(slot.path, error);
    CHECK(loaded);
    if (!loaded) { std::cerr << error << '\n'; return; }
    CHECK(copy.export_state().residents == before.residents);
    CHECK(copy.export_state().profession_cursor_id == before.profession_cursor_id);
    CHECK(world.checksum() == copy.checksum());
    for (int tick = 0; tick < 96; ++tick) {
        world.tick(); copy.tick();
        CHECK(world.checksum() == copy.checksum());
    }
    TempSave cargo_slot("ribterra_profession_deferred.ribsave");
    write_bytes(cargo_slot.path, read_bytes(std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                                          "world_save_v6_carry.ribsave"));
    World carrying(1, 0);
    CHECK(carrying.load(cargo_slot.path, error));
    const auto cargo = std::find_if(carrying.item_stacks().begin(), carrying.item_stacks().end(),
                                   [](const auto &stack) { return stack.location.kind == ItemLocationKind::Carried; });
    CHECK(cargo != carrying.item_stacks().end());
    if (cargo == carrying.item_stacks().end()) return;
    const int resident = cargo->location.entity_id;
    const auto changed = carrying.assign_profession({resident}, assignment);
    CHECK(changed.size() == 1 && changed.front().status == ResidentCommandStatus::Deferred);
    const auto deferred = carrying.export_state();
    const auto actor = std::find_if(deferred.residents.begin(), deferred.residents.end(),
                                    [=](const auto &r) { return r.id == resident; });
    CHECK(actor != deferred.residents.end() && actor->profession.deferred);
    CHECK(carrying.save(cargo_slot.path, error));
    CHECK(copy.load(cargo_slot.path, error));
    CHECK(copy.export_state().residents == deferred.residents);
    for (int tick = 0; tick < 128; ++tick) {
        carrying.tick(); copy.tick();
        CHECK(carrying.checksum() == copy.checksum());
    }
}

void forged_profession_payloads_reject_transactionally() {
    using namespace ribterra;
    TempSave source_slot("ribterra_profession_forgery_source.ribsave"), damaged("ribterra_profession_forgery_bad.ribsave");
    World world(70421, 2);
    std::string error;
    CHECK(world.save(source_slot.path, error));
    const auto original = read_bytes(source_slot.path);
    auto reject_bytes = [&](const std::vector<std::uint8_t> &bytes) {
        write_bytes(damaged.path, bytes);
        const auto checksum = world.checksum();
        CHECK(!world.load(damaged.path, error));
        CHECK(!error.empty() && world.checksum() == checksum);
        CHECK(read_bytes(damaged.path) == bytes);
        CHECK(!std::filesystem::exists(damaged.path.string() + ".v6.bak"));
    };
    auto reject = [&](const std::function<void(save::WorldSaveT &)> &mutate) {
        reject_bytes(mutate_inventory_payload(original, mutate));
    };
    reject([](auto &s) { s.residents.front()->additional_xp.clear(); });
    reject([](auto &s) { s.residents.front()->additional_xp.push_back(0); });
    reject([](auto &s) { s.residents.front()->skill_practice.pop_back(); });
    reject([](auto &s) { s.residents.front()->abilities_bp.pop_back(); });
    reject([](auto &s) { s.residents.front()->profession.reset(); });
    reject([](auto &s) { s.residents.front()->profession->priorities.pop_back(); });
    reject([](auto &s) { s.residents.front()->profession->priorities.front() = 6; });
    reject([](auto &s) { s.residents.front()->additional_xp.front() = -1; });
    reject([](auto &s) { s.residents.front()->additional_xp.front() = maximum_skill_experience + 1; });
    reject([](auto &s) { s.residents.front()->abilities_bp.front() = -1501; });
    reject([](auto &s) { s.residents.front()->abilities_bp.back() = 1501; });
    reject([](auto &s) { s.residents.front()->skill_practice.front()->experience_remainder = 4000; });
    reject([](auto &s) { s.residents.front()->skill_practice.front()->familiarity_debt = 30001; });
    reject([](auto &s) { s.residents.front()->skill_practice.front()->last_practice_tick = s.metadata->ticks + 1; });
    reject([](auto &s) { s.residents.front()->skill_practice.front()->familiarity_debt = 1; });
    reject([](auto &s) {
        s.residents.front()->wood_xp = maximum_skill_experience;
        s.residents.front()->skill_practice.front()->experience_remainder = 1;
    });
    reject([](auto &s) { s.residents.front()->profession->profession = static_cast<save::Profession>(255); });
    reject([](auto &s) { s.residents.front()->profession->wait_reason = static_cast<save::ProfessionWaitReason>(255); });
    reject([](auto &s) { s.residents.front()->profession->worksite = std::make_unique<save::Cell>(world_max, 0, save::Layer::Surface); });
    reject([](auto &s) { s.profession_cursor_id = 0; });
    auto masquerade = original;
    masquerade[20] = 2;
    masquerade[12] = 6; masquerade[16] = 5;
    constexpr std::string_view old_hash = "3a8700a4682e31d8c14606093966cc57a322afded4c82480dddf306d2a85fbbe";
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i < 32; ++i)
        masquerade[64 + i] = static_cast<std::uint8_t>((nibble(old_hash[i * 2]) << 4U) | nibble(old_hash[i * 2 + 1]));
    repair_container_checksum(masquerade);
    reject_bytes(masquerade);
    CHECK(error == "Invalid settlement payload.");
    const auto legacy = read_bytes(std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                                   "world_save_v6_work_search.ribsave");
    reject_bytes(mutate_inventory_payload(legacy, [](auto &s) { s.profession_cursor_id = 2; }));
    CHECK(error == "Legacy save contains profession fields.");
    auto bad_hash = legacy;
    bad_hash[64] ^= 1;
    repair_container_checksum(bad_hash);
    reject_bytes(bad_hash);
    CHECK(error == "Save content version is unsupported.");
    auto bad_rules = legacy;
    bad_rules[16] = 6;
    repair_container_checksum(bad_rules);
    reject_bytes(bad_rules);
    CHECK(error == "Version 6 rules or generator is unsupported.");
}
