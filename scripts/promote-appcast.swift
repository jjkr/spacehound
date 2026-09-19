#!/usr/bin/env swift
// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

// Moves one appcast item from the beta channel to the default (production)
// channel by deleting its <sparkle:channel> element, and removes older beta
// items that the promoted release supersedes. Nothing else in the feed is
// touched. The feed signature is invalidated by this edit, so the caller must
// re-sign the file with Sparkle's sign_update afterwards.

import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("error: \(message)\n".utf8))
    exit(EXIT_FAILURE)
}

guard CommandLine.arguments.count == 3 else {
    fail("usage: promote-appcast.swift APPCAST_PATH X.Y.Z")
}

let appcastURL = URL(fileURLWithPath: CommandLine.arguments[1])
let version = CommandLine.arguments[2]
let betaChannel = "beta"
let sparkleNamespace = "http://www.andymatuschak.org/xml-namespaces/sparkle"

let document: XMLDocument
do {
    document = try XMLDocument(contentsOf: appcastURL, options: [.nodePreserveAll])
} catch {
    fail("could not parse \(appcastURL.path): \(error.localizedDescription)")
}

guard let items = try? document.nodes(forXPath: "/rss/channel/item") as? [XMLElement] else {
    fail("appcast has no rss/channel/item elements")
}

func sparkleChild(_ name: String, of item: XMLElement) -> XMLElement? {
    return item.elements(forName: "sparkle:\(name)").first
        ?? item.elements(forLocalName: name, uri: sparkleNamespace).first
}

func itemVersion(_ item: XMLElement) -> String? {
    if let element = sparkleChild("version", of: item) {
        return element.stringValue
    }
    return item.elements(forName: "enclosure").first?
        .attribute(forName: "sparkle:version")?.stringValue
}

func numericVersion(_ text: String) -> [Int]? {
    let parts = text.split(separator: ".", omittingEmptySubsequences: false).map { Int($0) }
    guard parts.count == 3, !parts.contains(nil) else { return nil }
    return parts.compactMap { $0 }
}

guard let promotedVersion = numericVersion(version) else {
    fail("version must use X.Y.Z format, got \(version)")
}

let matches = items.filter { itemVersion($0) == version }
guard matches.count == 1, let item = matches.first else {
    fail("expected exactly one item for \(version) in the feed, found \(matches.count)")
}

guard let channel = sparkleChild("channel", of: item) else {
    fail("\(version) has no channel; it is already a production release")
}
guard channel.stringValue == betaChannel else {
    fail("\(version) is on channel '\(channel.stringValue ?? "")', not '\(betaChannel)'")
}

// Removes a node together with the whitespace text node that precedes it so
// the surrounding indentation stays tidy.
func remove(_ node: XMLNode, from parent: XMLElement) {
    let index = node.index
    if index > 0,
       let previous = parent.child(at: index - 1),
       previous.kind == .text,
       previous.stringValue?.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty == true {
        parent.removeChild(at: index - 1)
        parent.removeChild(at: index - 1)
    } else {
        parent.removeChild(at: index)
    }
}

remove(channel, from: item)

// Beta items below the promoted version can never be offered again: beta
// users also see the default channel, and Sparkle picks the highest version.
var removedVersions: [String] = []
for other in items where other !== item {
    guard let channelElement = sparkleChild("channel", of: other),
          channelElement.stringValue == betaChannel,
          let text = itemVersion(other),
          let otherVersion = numericVersion(text),
          otherVersion.lexicographicallyPrecedes(promotedVersion),
          let parent = other.parent as? XMLElement else {
        continue
    }
    remove(other, from: parent)
    removedVersions.append(text)
}

let output = document.xmlData(options: [.nodePreserveAll])
do {
    try output.write(to: appcastURL, options: .atomic)
} catch {
    fail("could not write \(appcastURL.path): \(error.localizedDescription)")
}

if removedVersions.isEmpty {
    print("Promoted \(version) to the production channel; the feed must be re-signed")
} else {
    print("Promoted \(version) to the production channel and removed superseded beta item(s) \(removedVersions.joined(separator: ", ")); the feed must be re-signed")
}
