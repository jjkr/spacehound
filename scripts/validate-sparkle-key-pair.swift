#!/usr/bin/env swift

import CryptoKit
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("error: \(message)\n".utf8))
    exit(EXIT_FAILURE)
}

guard CommandLine.arguments.count == 2 else {
    fail("usage: validate-sparkle-key-pair.swift PUBLIC_KEY_BASE64")
}

let publicKeyText = CommandLine.arguments[1]
let privateKeyInput = FileHandle.standardInput.readDataToEndOfFile()
guard let privateKeyText = String(data: privateKeyInput, encoding: .utf8)?
    .trimmingCharacters(in: .whitespacesAndNewlines),
    let configuredPublicKey = Data(base64Encoded: publicKeyText),
    configuredPublicKey.count == 32,
    let privateKeySecret = Data(base64Encoded: privateKeyText) else {
    fail("Sparkle keys must be valid base64 and the public key must decode to 32 bytes")
}

let derivedPublicKey: Data
switch privateKeySecret.count {
case 32:
    do {
        derivedPublicKey = try Curve25519.Signing.PrivateKey(
            rawRepresentation: privateKeySecret
        ).publicKey.rawRepresentation
    } catch {
        fail("could not derive the Sparkle public key from the private seed")
    }
case 96:
    // Sparkle's legacy exported format is its 64-byte private key followed by
    // the corresponding 32-byte public key.
    derivedPublicKey = privateKeySecret.suffix(32)
default:
    fail("Sparkle private key must decode to a 32-byte seed or 96-byte legacy key")
}

guard configuredPublicKey == derivedPublicKey else {
    fail("SPARKLE_PUBLIC_ED_KEY does not match SPARKLE_ED_PRIVATE_KEY")
}
