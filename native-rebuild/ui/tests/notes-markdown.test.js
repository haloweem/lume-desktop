import test from "node:test";
import assert from "node:assert/strict";
import { renderMarkdown } from "../notes-markdown.js";

test("inline formula renders a fraction and superscript as mathematical layout", () => {
  const html = renderMarkdown(String.raw`结果 $\frac{1}{2}+x^2$`);
  assert.match(html, /class="katex"/);
  assert.match(html, /<mfrac>/);
  assert.match(html, /<msup>/);
  assert.match(html, /class="katex-html"/);
});

test("display formula renders the sum with limits in display mode", () => {
  const html = renderMarkdown(String.raw`$$
\sum_{i=1}^{n} i
$$`);
  assert.match(html, /class="katex-display"/);
  assert.match(html, /<munderover>/);
  assert.match(html, /∑/);
});

test("formula text is isolated from Markdown emphasis and HTML", () => {
  const html = renderMarkdown(String.raw`$a*b*c$ **粗体** $\text{<script>}$`);
  assert.match(html, /class="katex"/);
  assert.doesNotMatch(html, /<em>|<script>/);
  assert.match(html, /<strong>粗体<\/strong>/);
});

test("unsupported formulas remain readable without throwing", () => {
  const html = renderMarkdown(String.raw`$\notARealCommand{x}$`);
  assert.match(html, /mathcolor="#ff9b85"/);
  assert.match(html, /notARealCommand/);
});

test("malformed formulas show a readable parse error", () => {
  const html = renderMarkdown(String.raw`$\frac{1}{$`);
  assert.match(html, /class="katex-error"/);
  assert.match(html, /title="ParseError:/);
});

test("formula commands cannot navigate, fetch images or inject arbitrary HTML", () => {
  const html = renderMarkdown(String.raw`$\href{javascript:alert(1)}{x}$ $\includegraphics{https://example.com/x.png}$ $\htmlClass{bad}{x}$`);
  assert.doesNotMatch(html, /<a\b|<img\b|class="bad"|href="javascript:/);
});

test("formula macro definitions do not leak between expressions", () => {
  const html = renderMarkdown(String.raw`$\gdef\lumenewmacro{x}\lumenewmacro$ $\lumenewmacro$`);
  assert.match(html, /class="katex"/);
  assert.match(html, /<mtext>\\lumenewmacro<\/mtext>/);
  assert.match(html, /mathcolor="#ff9b85"/);
});

test("recursive formulas have a bounded readable error", () => {
  const html = renderMarkdown(String.raw`$\def\again{\again}\again$`);
  assert.match(html, /class="katex-error"/);
  assert.ok(html.length < 5000);
});

test("code spans support variable backtick lengths and isolate formulas and emphasis", () => {
  assert.equal(renderMarkdown("``a`b $x^2$ **raw**``"), "<p><code>a`b $x^2$ **raw**</code></p>");
});

test("ordinary text decodes one entity layer and keeps code entities literal", () => {
  assert.equal(renderMarkdown("A &amp; B &amp;amp; &lt;b&gt; &#x3C;b&#62;"),
    "<p>A &amp; B &amp;amp; &lt;b&gt; &lt;b&gt;</p>");
  assert.equal(renderMarkdown("`A &amp; B &lt;b&gt;`"), "<p><code>A &amp;amp; B &amp;lt;b&amp;gt;</code></p>");
  assert.equal(renderMarkdown("&#42;literal&#42; &#36;x^2&#36;"), "<p>*literal* $x^2$</p>");
});

test("all CommonMark ASCII punctuation escapes become literal text", () => {
  assert.equal(renderMarkdown(String.raw`\! \" \# \$ \% \& \* \+ \, \- \. \/ \: \; \< \= \> \? \@ \[ \\ \] \^ \_ \` \{ \| \} \~`),
    "<p>! &quot; # $ % &amp; * + , - . / : ; &lt; = &gt; ? @ [ \\ ] ^ _ ` { | } ~</p>");
});

test("fenced code requires a matching fence of at least the opening length", () => {
  const markdown = "````js\nconst x = 1;\n```\n$x^2$ **raw**\n````\n\nend";
  assert.equal(renderMarkdown(markdown), "<pre><code>const x = 1;\n```\n$x^2$ **raw**</code></pre><p>end</p>");
});

test("fenced code closing line cannot contain trailing text", () => {
  assert.equal(renderMarkdown("```\nhello\n``` not a close\nworld\n```"), "<pre><code>hello\n``` not a close\nworld</code></pre>");
});

test("combined emphasis renders correctly within bold text", () => {
  assert.equal(renderMarkdown("**hello *world***"), "<p><strong>hello <em>world</em></strong></p>");
  assert.equal(renderMarkdown("***both***"), "<p><em><strong>both</strong></em></p>");
});

test("bold nested in italic never produces crossing HTML tags", () => {
  assert.equal(renderMarkdown("*hello **world***"), "<p><em>hello <strong>world</strong></em></p>");
  assert.equal(renderMarkdown("***hello** world*"), "<p><em><strong>hello</strong> world</em></p>");
  assert.equal(renderMarkdown("**hello __world** after__"), "<p><strong>hello __world</strong> after__</p>");
});

test("existing Markdown preserves its safe non-navigating link and image boundary", () => {
  const html = renderMarkdown('# 标题\n\n<script>alert(1)</script>\n\n- [x] 完成\n- 列表\n\n1. 一\n2. 二\n\n> 引用\n\n---\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n[危险](javascript:alert(1)) [好](https://example.com) ![图](https://example.com/x.png)');
  assert.match(html, /<h1>标题<\/h1>/);
  assert.match(html, /&lt;script&gt;/);
  assert.match(html, /MdCheck is-done/);
  assert.match(html, /<ol><li>一<\/li><li>二<\/li><\/ol>/);
  assert.match(html, /<blockquote><p>引用<\/p><\/blockquote>/);
  assert.match(html, /<hr>/);
  assert.match(html, /<table><thead>/);
  assert.match(html, /data-href="https:\/\/example.com"/);
  assert.doesNotMatch(html, /<script>|<img| href=|data-href="javascript/);
});

test("literal token-like input cannot impersonate a rendered fragment", () => {
  assert.equal(renderMarkdown("\0" + "0\0 `<script>`"), "<p>�0� <code>&lt;script&gt;</code></p>");
});

test("oversized formulas remain readable without entering the math renderer", () => {
  const html = renderMarkdown(`$${"x".repeat(8193)}$`);
  assert.match(html, /class="katex-error"/);
  assert.match(html, /8192-character limit/);
  assert.doesNotMatch(html, /class="katex"/);
});
