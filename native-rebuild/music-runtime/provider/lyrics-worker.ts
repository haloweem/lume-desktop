import { parseLyricsByFormat, type LyricParseFormat } from '../vendor/folia/src/utils/lyrics/parserCore';
import type { LyricProcessingOptions } from '../vendor/folia/src/utils/lyrics/types';

// Parsing already runs outside Lume's UI in this private Node host; preserve Folia's parser itself.
export const parseLyricsAsync = async (format: LyricParseFormat, content: string, translation = '',
    options?: LyricProcessingOptions, romanization = '') => (
    parseLyricsByFormat(format, content, translation, options, romanization)
);
