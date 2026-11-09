import de from "./de";
import en from "./en";
import es from "./es";
import fr from "./fr";

export const dictionaries = { en, de, es, fr };
export type Lang = keyof typeof dictionaries;
export const langs = Object.keys(dictionaries) as Lang[];
export const homePath = (lang: Lang) => (lang === "en" ? "/" : `/${lang}/`);
