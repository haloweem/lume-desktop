
import {buildTimedWords} from 'reference:timing';
import {annotateLyricLines} from 'H:/refactor/history/lume1.0.1/third_party/folia-major/upstream/src/utils/lyrics/renderHints.ts';
export {themes} from 'reference:preference-theme';
export function prepareLines(lines) {
  return annotateLyricLines(lines.map(line=>{
    if(!line||typeof line.fullText!=='string'||!Number.isFinite(line.startTime)||!Number.isFinite(line.endTime)||line.startTime<0||line.endTime<line.startTime)
      throw new TypeError('Invalid lyric line');
    const authored=Array.isArray(line.words)&&line.words.length>0;
    if(authored && line.words.some(word=>!word||typeof word.text!=='string'||!Number.isFinite(word.startTime)||!Number.isFinite(word.endTime)||word.endTime<word.startTime))
      throw new TypeError('Invalid lyric word');
    return {...line,words:authored?line.words:buildTimedWords(line.fullText,line.startTime,line.endTime),
      referenceTiming:authored?'authored-words':'estimated-from-line',isWordByWord:authored};
  }));
}
