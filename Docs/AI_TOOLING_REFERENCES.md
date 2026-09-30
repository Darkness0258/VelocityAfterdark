# AI tooling references

Checked 2026-09-29 for the Velocity Afterdark Unreal Engine project.

## Browser automation

- [Ego (lite)](https://lite.ego.app/) is a Chromium browser with an agent-control CLI called `ego-browser`. The globally installed `ego-browser` skill is present at `C:\Users\mhamz\.agents\skills\ego-browser`, but this machine has no `ego-browser` command and the runtime was not verified.
- The upstream [Ego (lite) installation guide](https://github.com/citrolabs/ego-lite/blob/main/skills/ego-browser/references/install.md) currently documents a macOS installer and directs Windows users to the download/waitlist page. The CLI is provided by the desktop app after onboarding; `ego-browser` is not an npm package (`npm view ego-browser` returned 404).
- [Playwright CLI skill](https://github.com/microsoft/playwright-cli) is also installed globally, and `playwright-cli` is available on PATH. It is a separate automation route and does not provide Ego Lite's shared browser profile.
- [Chrome DevTools MCP](https://github.com/ChromeDevTools/chrome-devtools-mcp) is another separate browser debugging/automation option. It is not Ego Lite.
- [actionlint](https://github.com/rhysd/actionlint) checks GitHub Actions workflow files. It is unrelated to browser automation and is not an Ego Lite wrapper.
- `https://mcp.sentry.dev/mcp` is a Sentry MCP endpoint, not a browser runtime.

## Skills and skill tooling

- [Vercel Skills CLI](https://github.com/vercel-labs/skills) installs and manages agent skills; it is tooling for skills, not a browser.
- [Anthropic Skills](https://github.com/anthropics/skills) is Anthropic's public skills repository.
- [Anthropic Claude Plugins Official](https://github.com/anthropics/claude-plugins-official) is their managed plugin directory.
- [Composio Awesome Claude Skills](https://github.com/ComposioHQ/awesome-claude-skills) is a large community catalogue. It is distinct from Anthropic's official skills repository.
- Other skill or design collections in the list: [mattpocock/skills](https://github.com/mattpocock/skills), [addyosmani/agent-skills](https://github.com/addyosmani/agent-skills), [emilkowalski/skills](https://github.com/emilkowalski/skills), [multica-ai/andrej-karpathy-skills](https://github.com/multica-ai/andrej-karpathy-skills), [voltagent/awesome-design-md](https://github.com/voltagent/awesome-design-md), [nexu-io/open-design](https://github.com/nexu-io/open-design), and [pbakaus/impeccable](https://github.com/pbakaus/impeccable).
- [tt-a1i/archify](https://github.com/tt-a1i/archify) is a diagramming skill/tool for architecture, workflows, sequence and data-flow diagrams. The skill is already installed globally in this environment.
- Broader AI project/skill directories: [awesome-opensource-ai](https://github.com/alvinreal/awesome-opensource-ai), [awesome-ai-apps](https://github.com/Arindam200/awesome-ai-apps), [awesome-ai-agents-2026](https://github.com/ARUNAGIRINATHAN-K/awesome-ai-agents-2026), and [awesome-generative-ai](https://github.com/steven2358/awesome-generative-ai).

## Agent frameworks and applications

- [obra/superpowers](https://github.com/obra/superpowers) is an agent skills/framework and software development workflow.
- [get-shit-done](https://github.com/gsd-build/get-shit-done) is a spec-driven development system.
- [Cline](https://github.com/cline/cline), [CrewAI](https://github.com/crewaiinc/crewai), and [MiroThinker](https://github.com/MiroMindAI/MiroThinker) are agent/coding tools or frameworks.
- [Pipecat](https://github.com/pipecat-ai/pipecat) is a voice and multimodal conversational AI framework.
- [AnythingLLM](https://github.com/mintplex-labs/anything-llm) is an LLM and knowledge-work application.
- [Postiz](https://github.com/gitroomhq/postiz-app) is a social-media management application.
- [Firecrawl](https://github.com/firecrawl/firecrawl) is a web crawling/scraping service and codebase.
- [Three.js](https://github.com/mrdoob/three.js) and [React Three Fiber](https://github.com/pmndrs/react-three-fiber) are web 3D libraries; they are separate from this Unreal Engine project.

## Duplicates and cleanup

These URLs were repeated; each is listed once above: `voltagent/awesome-design-md`, `pbakaus/impeccable`, `ayghri/i-have-adhd`, `anthropics/claude-plugins-official`, and `obra/superpowers`.

`github.com/topics/motion-design?o=asc&s=stars` is a GitHub topic page, not a repository. The line `npx skillfish add nexu-io/open-design image-to-code-skill` is an installation command rather than a project URL; verify the intended skill/package in the upstream project's install docs before running it.

## Local state summary

- Present: `ego-browser` skill, `playwright-cli` skill and executable, `archify` skill; Codex MCP registrations include `node_repl`, `serena`, and `icons8mcp`.
- Ego Lite desktop/runtime: not detected; Windows installation is not documented as available by its current upstream installation guide.
- This is an Unreal Engine native game project. Browser tools may help with web research or web-based companion tooling, but do not replace Unreal's editor, build, package, and game acceptance workflows.
