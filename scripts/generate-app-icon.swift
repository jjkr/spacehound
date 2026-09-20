#!/usr/bin/env swift
// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

// Renders every size in the AppIcon asset catalog from logo.png. The logo is
// white on transparent, so it is placed on a black rounded square that follows
// the macOS icon grid (824pt of the 1024pt canvas, 185pt corner radius);
// otherwise the icon would be invisible in light mode.

import AppKit

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("error: \(message)\n".utf8))
    exit(EXIT_FAILURE)
}

guard CommandLine.arguments.count == 3 else {
    fail("usage: generate-app-icon.swift LOGO_PNG APPICONSET_DIR")
}

let logoPath = CommandLine.arguments[1]
let outputDirectory = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)

guard let logo = NSImage(contentsOfFile: logoPath),
      let logoRep = NSBitmapImageRep(data: try Data(contentsOf: URL(fileURLWithPath: logoPath)))
else {
    fail("could not read \(logoPath)")
}

// Crop to the logo's visible pixels so the artwork, not the file's padding,
// is what gets centered on the plate.
var minX = logoRep.pixelsWide, minY = logoRep.pixelsHigh, maxX = -1, maxY = -1
for y in 0..<logoRep.pixelsHigh {
    for x in 0..<logoRep.pixelsWide where (logoRep.colorAt(x: x, y: y)?.alphaComponent ?? 0) > 0.02 {
        minX = min(minX, x); maxX = max(maxX, x)
        minY = min(minY, y); maxY = max(maxY, y)
    }
}
guard maxX >= minX, maxY >= minY else { fail("logo has no visible pixels") }
// NSBitmapImageRep rows are top-down; NSImage drawing is bottom-up.
let visible = NSRect(
    x: CGFloat(minX),
    y: CGFloat(logoRep.pixelsHigh - 1 - maxY),
    width: CGFloat(maxX - minX + 1),
    height: CGFloat(maxY - minY + 1)
)

// Geometry in 1024pt canvas units.
let canvas: CGFloat = 1024
let plateInset: CGFloat = 100
let plateRadius: CGFloat = 185
let artworkInset: CGFloat = 48

func render(pixels: Int) -> Data {
    let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0
    )!
    NSGraphicsContext.saveGraphicsState()
    let context = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.current = context
    context.imageInterpolation = .high

    let scale = CGFloat(pixels) / canvas
    let plate = NSRect(x: plateInset, y: plateInset, width: canvas - 2 * plateInset, height: canvas - 2 * plateInset)
    let plateScaled = NSRect(x: plate.minX * scale, y: plate.minY * scale, width: plate.width * scale, height: plate.height * scale)
    NSColor.black.setFill()
    NSBezierPath(roundedRect: plateScaled, xRadius: plateRadius * scale, yRadius: plateRadius * scale).fill()

    let available = plate.insetBy(dx: artworkInset, dy: artworkInset)
    let fit = min(available.width / visible.width, available.height / visible.height)
    let drawn = NSRect(
        x: available.midX - visible.width * fit / 2,
        y: available.midY - visible.height * fit / 2,
        width: visible.width * fit,
        height: visible.height * fit
    )
    let drawnScaled = NSRect(x: drawn.minX * scale, y: drawn.minY * scale, width: drawn.width * scale, height: drawn.height * scale)
    // Draw from the source rect in the logo's point space, which for a
    // 1x PNG matches pixel space.
    let pointsPerPixel = logo.size.width / CGFloat(logoRep.pixelsWide)
    let source = NSRect(
        x: visible.minX * pointsPerPixel, y: visible.minY * pointsPerPixel,
        width: visible.width * pointsPerPixel, height: visible.height * pointsPerPixel
    )
    logo.draw(in: drawnScaled, from: source, operation: .sourceOver, fraction: 1)

    NSGraphicsContext.restoreGraphicsState()
    guard let png = rep.representation(using: .png, properties: [:]) else { fail("could not encode PNG") }
    return png
}

for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let name = scale == 1 ? "icon_\(size)x\(size).png" : "icon_\(size)x\(size)@2x.png"
        let url = outputDirectory.appendingPathComponent(name)
        do {
            try render(pixels: size * scale).write(to: url)
        } catch {
            fail("could not write \(url.path): \(error)")
        }
        print("wrote \(name)")
    }
}
