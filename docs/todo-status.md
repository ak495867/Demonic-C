# TODO Completion Status

Items 11 through 22 are implemented in the current project.

| Item | Status | Implementation |
|---|---|---|
| 11. Built-in testing | Complete | The lexer recognizes `#[test]` and `#[bench]`; the compiler emits executable C harnesses; declared suites are compiled and executed by the host runner; benchmark execution records elapsed time. The integration suite covers JSON, datetime, regex, SQLite, and HTTP behavior. |
| 12. Test commands | Complete | `dmc test`, `dmc test --coverage`, and `dmc test --bench` execute declared suites. Coverage writes `docs/coverage.txt` with discovered files, source lines, executable lines, and inventory percentage. Benchmark mode executes benchmark harnesses and reports timings. |
| 13. Documentation generator | Complete | `dmc doc` generates HTML from DMC documentation blocks and structured Markdown API documents with descriptions, parameters, returns, and examples. |
| 14. Documentation commands | Complete | `dmc doc`, `dmc doc --serve`, and `dmc doc --publish` generate documentation, start a local HTTP preview server, and publish the generated page to the local package registry. |
| 15. JSON module | Complete | The generated runtime provides JSON text parsing, stringification, and key extraction through `json_parse`, `json_stringify`, and `json_get`. The integration suite verifies object-key extraction. |
| 16. HTTP client | Complete | The generated runtime performs bounded HTTP GET requests through curl, returns response text, reports HTTP status codes, rejects unsafe shell metacharacters, and maps connection failures to a negative status. |
| 17. Date/time | Complete | `datetime_now`, `datetime_format`, and `datetime_unix` use host time APIs and are covered by the integration suite. |
| 18. Regex | Complete | `regex_new`, `regex_match`, and `regex_free` use POSIX regular expressions and are covered by the integration suite. |
| 19. Database client | Complete | With `DMC_SQLITE`, generated programs use SQLite open, query, row extraction, and close operations. The host runner links the available SQLite shared library and the integration suite executes `SELECT 1` against an in-memory database. |
| 20. Binary distribution | Complete | `dmc build --release`, `dmc build --static`, and `dmc bundle` work. Release and static builds produce binaries, and bundle creates `dist/dmc-project.tar.gz`. |
| 21. Package publishing | Complete | `dmc login`, `dmc publish`, and `dmc deprecate package@version` persist authenticated state, package metadata, and deprecation records under `packages/local-registry/`. |
| 22. CI/CD templates | Complete | `.dmc/ci.yml` describes test, lint, documentation, release-build, and bundle steps. `dmc ci` executes the same workflow locally and reports failure if any step fails. |

## Validation Artifacts

The current validation run produced `docs/coverage.txt`, `docs/generated/index.html`, `dist/dmc-project.tar.gz`, release and static binaries under `bin/`, local registry records under `packages/local-registry/`, and passing DMC integration tests.
