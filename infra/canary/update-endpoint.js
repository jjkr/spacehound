const synthetics = require("Synthetics");

const REQUEST_TIMEOUT_MS = 30_000;

async function checkedFetch(url, method = "GET") {
  const response = await fetch(url, {
    method,
    redirect: "error",
    signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
  });
  if (response.status !== 200) {
    throw new Error(`${method} ${url} returned HTTP ${response.status}`);
  }
  return response;
}

async function checkHead(url, expectedHost) {
  const parsed = new URL(url);
  if (parsed.protocol !== "https:" || parsed.hostname !== expectedHost) {
    throw new Error(`Unexpected download URL: ${url}`);
  }
  const response = await checkedFetch(parsed.toString(), "HEAD");
  const contentLength = Number(response.headers.get("content-length"));
  if (!Number.isFinite(contentLength) || contentLength <= 0) {
    throw new Error(`HEAD ${url} did not return a positive content-length`);
  }
}

exports.handler = async () => {
  const baseUrl = process.env.BASE_URL;
  if (!baseUrl) {
    throw new Error("BASE_URL is required");
  }
  const base = new URL(baseUrl);
  let appcastXml;
  let enclosureUrl;

  await synthetics.executeStep("fetch-appcast", async () => {
    const response = await checkedFetch(new URL("/appcast.xml", base).toString());
    appcastXml = await response.text();
    if (appcastXml.length === 0) {
      throw new Error("appcast.xml is empty");
    }
  });

  await synthetics.executeStep("validate-appcast", async () => {
    const page = await synthetics.getPage();
    const parsed = await page.evaluate((xml) => {
      const document = new DOMParser().parseFromString(xml, "application/xml");
      const parserError = document.querySelector("parsererror");
      const enclosure = document.querySelector("rss channel item enclosure");
      return {
        error: parserError?.textContent ?? null,
        enclosureUrl: enclosure?.getAttribute("url") ?? null,
      };
    }, appcastXml);
    if (parsed.error) {
      throw new Error(`appcast.xml is invalid XML: ${parsed.error}`);
    }
    if (!parsed.enclosureUrl) {
      throw new Error("appcast.xml has no current enclosure URL");
    }
    enclosureUrl = parsed.enclosureUrl;
  });

  await synthetics.executeStep("check-current-enclosure", async () => {
    await checkHead(enclosureUrl, base.hostname);
  });

  await synthetics.executeStep("check-latest-dmg", async () => {
    await checkHead(
      new URL("/releases/latest/SpaceRabbit-arm64.dmg", base).toString(),
      base.hostname,
    );
  });
};
