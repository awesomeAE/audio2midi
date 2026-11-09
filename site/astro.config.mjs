import { defineConfig, fontProviders } from "astro/config";
import sitemap from "@astrojs/sitemap";

export default defineConfig({
  site: "https://awesomeae.com",
  output: "static",
  integrations: [
    sitemap({
      i18n: { defaultLocale: "en", locales: { en: "en", de: "de", es: "es", fr: "fr" } },
    }),
  ],
  fonts: [
    {
      name: "Inter Tight",
      cssVariable: "--font-display",
      provider: fontProviders.google(),
      weights: [500, 600, 700],
      fallbacks: ["system-ui", "sans-serif"],
    },
    {
      name: "Inter",
      cssVariable: "--font-body",
      provider: fontProviders.google(),
      weights: [400, 500, 600],
      fallbacks: ["system-ui", "sans-serif"],
    },
    {
      name: "JetBrains Mono",
      cssVariable: "--font-mono",
      provider: fontProviders.google(),
      weights: [500],
      fallbacks: ["ui-monospace", "monospace"],
    },
  ],
});
