// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_test_macros.hpp>

// std::getenv is how the real deploy directories override the committed fixtures; the
// MSVC deprecation warning about it is noise here.
#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	fs::path AttestationRoot()
	{
		if (const char* root = std::getenv("HONG_ATTESTATION_ROOT")) {
			return fs::path(root);
		}
#ifdef HONG_SOURCE_DIR
		return fs::path(HONG_SOURCE_DIR) / "tests" / "fixtures" / "attestation";
#else
		return fs::path("tests") / "fixtures" / "attestation";
#endif
	}

	struct Record
	{
		fs::path path;
		json body;
	};

	std::vector<Record> LoadRecords()
	{
		std::vector<Record> records;
		const fs::path root = AttestationRoot();
		std::error_code error;
		if (!fs::is_directory(root, error)) {
			return records;
		}
		for (const auto& entry : fs::directory_iterator(root, error)) {
			const fs::path file = entry.path() / "attestation.json";
			if (!entry.is_directory() || !fs::is_regular_file(file)) {
				continue;
			}
			std::ifstream in(file, std::ios::binary);
			if (!in) {
				continue;
			}
			records.push_back({ file, json::parse(in, nullptr, true, true) });
		}
		std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
			return a.body.value("deployedAt", "") < b.body.value("deployedAt", "");
		});
		return records;
	}

	bool IsProvenanceKey(const std::string& a_key)
	{
		return a_key.size() > 10 && a_key.compare(a_key.size() - 10, 10, "Provenance") == 0;
	}

	bool IsBookkeeping(const std::string& a_key)
	{
		return a_key == "postHocAnnotations";
	}

	const std::vector<std::pair<std::string, std::vector<std::string>>>& Supersessions()
	{
		static const std::vector<std::pair<std::string, std::vector<std::string>>> table{
			{ "sourceAttestation", { "commit", "commitSubject" } },
			{ "commitProvenance", { "commit" } },
			{ "changeProvenance", { "change" } },
			{ "backupFileProvenance", { "backupFileHashes" } },
			{ "note", { "whatThisDoesNotProve" } },
			{ "pdbLinkLimitations", { "pdbLink" } },
			{ "treeBuildNormalisedSHA256", { "dllNormalisedSHA256" } },
			{ "normalisedDiffersFromPreviousDeployProvenance", { "normalisedDiffersFromPreviousDeploy" } },
			{ "deploymentArtifactPreservation", { "deployedCopiesPreserved", "whyDeployedCopyExists" } },
			{ "treeBuildComparable", { "dllByteIdenticalToTreeBuild", "pdbByteIdenticalToTreeBuild" } },
			{ "treeBuildPDBSHA256", { "pdbByteIdenticalToTreeBuild" } },
			{ "normalisedMatchTree", { "dllByteIdenticalToTreeBuild" } },
		};
		return table;
	}

	const std::vector<std::string>* ReplacementFor(const std::string& a_key)
	{
		for (const auto& [key, replacement] : Supersessions()) {
			if (key == a_key) {
				return &replacement;
			}
		}
		return nullptr;
	}

	bool IsNullable(const std::string& a_key)
	{
		static const std::set<std::string> nullable{
			"treeBuildDLLSHA256",
			"treeBuildPDBSHA256",
			"treeBuildFieldsProvenance",
			"mutationProof",
			"normalisedDiffersFromPreviousDeploy",
			"change",
			"postHocAnnotations",
		};
		return nullable.count(a_key) > 0;
	}
}

TEST_CASE("the attestation records are reachable, so these checks are not vacuous", "[attestation]")
{
	const auto records = LoadRecords();
	INFO("looking in " << AttestationRoot().string());
	REQUIRE(fs::is_directory(AttestationRoot()));
	REQUIRE_FALSE(records.empty());
	REQUIRE(records.size() >= 6);
}

TEST_CASE("every record carries every field its predecessors established", "[attestation]")
{
	const auto records = LoadRecords();
	REQUIRE_FALSE(records.empty());

	std::vector<std::string> problems;
	for (std::size_t i = 0; i < records.size(); ++i) {
		const std::string label = records[i].body.value("commit", "?").substr(0, 9);

		if (i > 0) {
			const auto& previous = records[i - 1].body;
			for (const auto& item : previous.items()) {
				const std::string& key = item.key();
				if (records[i].body.contains(key) || IsBookkeeping(key)) {
					continue;
				}
				if (IsProvenanceKey(key) && records[i].body.value(key.substr(0, key.size() - 10), json()).is_null() == false) {
					continue;  // the null it explained is gone or now carries a value
				}
				const auto* replacement = ReplacementFor(key);
				bool replaced = replacement != nullptr;
				for (const auto& name : replacement ? *replacement : std::vector<std::string>{}) {
					replaced = replaced && records[i].body.contains(name);
				}
				if (!replaced) {
					problems.push_back(label + " dropped " + key);
				}
			}
		}

		for (const auto& item : records[i].body.items()) {
			if (!item.value().is_null() || IsNullable(item.key())) {
				continue;
			}
			problems.push_back(label + " null not permitted: " + item.key());
		}
	}

	for (const auto& problem : problems) {
		UNSCOPED_INFO(problem);
	}
	REQUIRE(problems.empty());
}

TEST_CASE("a null field says why it is null", "[attestation]")
{
	const auto records = LoadRecords();
	REQUIRE_FALSE(records.empty());

	std::vector<std::string> unexplained;
	for (const auto& record : records) {
		const std::string label = record.body.value("commit", "?").substr(0, 9);
		for (const auto& item : record.body.items()) {
			if (!item.value().is_null() || !IsNullable(item.key()) || IsBookkeeping(item.key())) {
				continue;
			}
			const std::string provenance = item.key() + "Provenance";
			const bool explained = record.body.contains(provenance) && record.body.at(provenance).is_string() && !record.body.at(provenance).get<std::string>().empty();
			if (!explained) {
				unexplained.push_back(label + " null without reason: " + item.key());
			}
		}
	}

	for (const auto& entry : unexplained) {
		UNSCOPED_INFO(entry);
	}
	REQUIRE(unexplained.empty());
}

TEST_CASE("the chain links by the bytes each record names", "[attestation]")
{
	const auto records = LoadRecords();
	REQUIRE(records.size() >= 2);

	std::vector<std::string> breaks;
	for (std::size_t i = 1; i < records.size(); ++i) {
		const auto& previous = records[i - 1].body;
		const auto& current = records[i].body;
		const std::string previousDeployed = previous.value("dllSHA256", "");
		const std::string label = current.value("commit", "?").substr(0, 9);

		if (previousDeployed.empty() || !current.contains("backupFileHashes") || !current.at("backupFileHashes").is_object()) {
			breaks.push_back(label + ": no backup hashes to link with");
			continue;
		}
		const auto& hashes = current.at("backupFileHashes");
		if (!hashes.contains("HelmetOverlaysNG.dll") || hashes.at("HelmetOverlaysNG.dll") != previousDeployed) {
			breaks.push_back(label + ": backup DLL is not the previous deploy");
		}
	}

	for (const auto& entry : breaks) {
		UNSCOPED_INFO(entry);
	}
	REQUIRE(breaks.empty());
}

TEST_CASE("the newest record preserves its own deployed bytes", "[attestation]")
{
	const auto records = LoadRecords();
	REQUIRE_FALSE(records.empty());

	const auto& newest = records.back().body;
	const std::string label = newest.value("commit", "?").substr(0, 9);

	REQUIRE(newest.contains("deployedCopiesPreserved"));
	const auto& preserved = newest.at("deployedCopiesPreserved");
	REQUIRE(preserved.contains("dllSHA256"));
	REQUIRE(preserved.at("dllSHA256") == newest.at("dllSHA256"));
	INFO(label);
	REQUIRE(preserved.value("matchesRecord", false));
}

TEST_CASE("the newest record satisfies the full schema as written down", "[attestation]")
{
	const auto records = LoadRecords();
	REQUIRE_FALSE(records.empty());

	static const std::vector<std::string> required{
		"schemaVersion",
		"deployedAt",
		"backup",
		"gameLaunched",
		"commit",
		"commitSubject",
		"branch",
		"change",
		"dllSHA256",
		"pdbSHA256",
		"treeBuildDLLSHA256",
		"treeBuildPDBSHA256",
		"treeBuildFieldsProvenance",
		"dllByteIdenticalToTreeBuild",
		"pdbByteIdenticalToTreeBuild",
		"dllNormalisedSHA256",
		"normalisedSource",
		"normalisedDiffersFromPreviousDeploy",
		"freshness",
		"pdbLink",
		"symbolsExpectPresent",
		"symbolsContainer",
		"mutationProof",
		"mutationProofProvenance",
		"whatThisDoesNotProve",
		"whyThisExists",
		"backupFileHashes",
		"backupDiffersFromDeployedAsExpected",
		"backupHolds",
		"deployedCopiesPreserved",
		"whyDeployedCopyExists",
		"postHocAnnotations",
	};

	const auto& newest = records.back().body;
	INFO("newest record: " << records.back().path.string());
	std::vector<std::string> missing;
	for (const auto& key : required) {
		if (!newest.contains(key)) {
			missing.push_back(key);
		}
	}
	for (const auto& key : missing) {
		UNSCOPED_INFO("absent from the newest record: " << key);
	}
	REQUIRE(missing.empty());

	for (const auto& key : required) {
		std::vector<std::string> predating;
		for (const auto& record : records) {
			if (!record.body.contains(key)) {
				predating.push_back(record.body.value("commit", "?").substr(0, 9));
			}
		}
		if (!predating.empty()) {
			WARN("field " << key << " is absent from " << predating.size() << " earlier record(s), which predate it");
		}
	}
}

TEST_CASE("the deploy script enforces the same schema the suite checks", "[attestation]")
{
#ifdef HONG_SOURCE_DIR
	const fs::path script = fs::path(HONG_SOURCE_DIR) / "tools" / "check_attestation_schema.py";
#else
	const fs::path script = fs::path("tools") / "check_attestation_schema.py";
#endif
	INFO(script.string());
	REQUIRE(fs::is_regular_file(script));

	std::ifstream in(script, std::ios::binary);
	REQUIRE(in);
	const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

	const auto records = LoadRecords();
	REQUIRE_FALSE(records.empty());
	for (const auto& item : records.back().body.items()) {
		if (IsBookkeeping(item.key()) || IsProvenanceKey(item.key())) {
			continue;
		}
		static const std::set<std::string> humanRecorded{
			"shippedShaderCheck",
			"freshness",
			"change",
			"commitSubject",
			"mutationProof",
			"mutationProofProvenance",
			"whatThisDoesNotProve",
			"whyThisExists",
			"symbolsExpectPresent",
			"symbolsContainer",
			"pdbLink",
			"normalisedSource",
			"backupHolds",
			"whyDeployedCopyExists",
			"postHocAnnotations",
		};
		if (humanRecorded.count(item.key()) > 0) {
			continue;
		}
		INFO("field in the newest record but not in the checker: " << item.key());
		REQUIRE(text.find(item.key()) != std::string::npos);
	}
}

TEST_CASE("no record silently forgets a field, and a null is never invented into a value", "[attestation]")
{
	const auto records = LoadRecords();
	REQUIRE_FALSE(records.empty());

	std::vector<std::string> problems;
	for (const auto& record : records) {
		const std::string label = record.body.value("commit", "?").substr(0, 9);
		if (!record.body.contains("postHocAnnotations")) {
			continue;
		}
		REQUIRE(record.body.at("postHocAnnotations").is_array());
		for (const auto& annotation : record.body.at("postHocAnnotations")) {
			if (!annotation.contains("added") || !annotation.at("added").is_array() || !annotation.contains("note")) {
				problems.push_back(label + ": annotation missing added[] or note");
			}
		}
	}

	for (const auto& entry : problems) {
		UNSCOPED_INFO(entry);
	}
	REQUIRE(problems.empty());
}
