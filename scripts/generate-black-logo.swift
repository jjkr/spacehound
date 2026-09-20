#!/usr/bin/env swift
// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

// Writes a black-on-transparent copy of the white-on-transparent logo for
// light backgrounds, such as the README on GitHub's light theme. Only the
// color channels are inverted; alpha is kept, so the shape is unchanged.

import AppKit

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("error: \(message)\n".utf8))
    exit(EXIT_FAILURE)
}

guard CommandLine.arguments.count == 3 else {
    fail("usage: generate-black-logo.swift LOGO_PNG OUTPUT_PNG")
}

let inputPath = CommandLine.arguments[1]
let outputPath = CommandLine.arguments[2]

guard let source = NSBitmapImageRep(data: try Data(contentsOf: URL(fileURLWithPath: inputPath))) else {
    fail("could not read \(inputPath)")
}

let output = NSBitmapImageRep(
    bitmapDataPlanes: nil, pixelsWide: source.pixelsWide, pixelsHigh: source.pixelsHigh,
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0
)!

for y in 0..<source.pixelsHigh {
    for x in 0..<source.pixelsWide {
        guard let color = source.colorAt(x: x, y: y)?.usingColorSpace(.deviceRGB) else { continue }
        output.setColor(
            NSColor(
                deviceRed: 1 - color.redComponent,
                green: 1 - color.greenComponent,
                blue: 1 - color.blueComponent,
                alpha: color.alphaComponent
            ),
            atX: x, y: y
        )
    }
}

guard let png = output.representation(using: .png, properties: [:]) else { fail("could not encode PNG") }
do {
    try png.write(to: URL(fileURLWithPath: outputPath))
} catch {
    fail("could not write \(outputPath): \(error)")
}
print("wrote \(outputPath)")
