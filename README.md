# Mill Manager (C++ / Qt)

This is a standalone C++17/Qt 6 project containing the coffee and grain
products. It uses its own source, CMake configuration, and bundled logo.

## Build and run

Install a C++ compiler, CMake 3.24+, and Qt 6 development modules for Widgets,
Network, SQL (including the SQLite driver), and PrintSupport. From this folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/MillManager       # coffee
./build/MillManagerF      # grain
```

Run the built-in database, report/PDF, and workflow checks without opening the
main window:

```sh
./build/MillManager --self-test
./build/MillManagerF --self-test
```

Check a database copy against its matching edition without opening the main
window. The check initializes the database and migrates the legacy shared grain
schema when needed, so always point it at a disposable copy:

```sh
./build/MillManager --check-database /path/to/coffee-copy.db
./build/MillManagerF --check-database /path/to/grain-copy.db
```

On Debian/Ubuntu amd64, build a combined installable package with:

```sh
./package_linux.sh
```

The package is written to `dist/mill-manager-cpp_23.0.0-9_amd64.deb` and
installs separate coffee/grain launchers and desktop shortcuts. A compressed
source archive is also written to `dist/Mill_Manager_CPP_23.0.0_source.tar.gz`.

On Windows, open a Qt 6.3+ command prompt with CMake, `windeployqt`, and NSIS
available on `PATH`, then run:

```bat
build_windows.bat
```

This builds both editions, runs their self-tests, deploys the Qt runtime, and
creates an NSIS installer. Set `CMAKE_GENERATOR` in the environment to select
a specific installed generator such as `NMake Makefiles` or `MinGW Makefiles`.
CTest runs both edition self-tests as part of Linux and Windows packaging.

Both executables keep separate business databases in the current user's
Documents folder. Back up business data before first use.
Use **Switch account** at the bottom of the desktop navigation sidebar to sign
out and return to the login prompt. Confirming closes the current user session
and rebuilds the app after the next account signs in; already-saved records
remain unchanged.

The Qt main window now follows the Python app's visual shell: teal branded
header and logo, signed-in role and live clock, minimize/maximize controls,
dark navigation sidebar, highlighted active page, and a light page surface.
The management dashboard now uses a four-column metric-card layout for cash,
bank, product stock, purchases, sales, receipts, customer credit, milling,
expenses, payroll, and supplier payables. Currency is displayed with grouping
commas and a `UGX` suffix, and weights use two decimal places and a `kg`
suffix. The dashboard highlights a negative cash balance and uses the same
purchase, milling, sale, money, and reports shortcuts as the Python editions;
figures refresh every three seconds.
Business tables use consistent shaded headers, alternating rows, readable
row heights, right-aligned/grouped numeric values, and resizable columns.
Long forms and reports scroll instead of clipping at smaller window sizes.
Buttons use a compact, consistent desktop style with clear hover, pressed,
focus, and disabled states; the navigation and window controls have matching
smaller proportions.
Admins can customize the appearance in **Settings > Appearance**: enable dark
mode, choose the accent/button color and page background color, or select a
local background image. Save appearance to apply it immediately and retain it
in that edition's business database for the next launch. The selected image
must remain at its chosen file path.
The C++ version also includes a rich-text Document Studio with open/save,
basic text formatting, undo/redo, find/replace, PDF export, and a standard
printer dialog. KIYAMBI is an offline business-intelligence assistant. Its
live review is grounded in recorded costs, purchase-quality fields, cash and
bank balances, receivables, payables, and recent customers. It organizes
recommendations around cost reduction, quality control, strategic planning,
competitive advantage, risk management, and time savings; it also answers
questions about balances, stock, credits, and purchase history. Recommendations
are decision support, not a replacement for checking source records or
professional accounting advice.
The chatbot recognizes supported natural-language topics and keeps follow-up
context during the current session. Its business-idea generator composes
suggestions from sales, expense, credit, and cash data from the last 90 days.
It is a local, rule-and-template-based assistant rather than a large language
model: it does not invent free-form answers or send business data to an online
AI service.

KIYAMBI can create surveys for customers, farmers and suppliers, staff, or a
general audience; collect named or anonymous text responses; and show a
response summary. Survey definitions and answers are saved to the active
business database and are not sent to an online service. Back up the database
as usual to retain survey records. The bundled phrase CSV is copied beside
both built targets.
The Qt pages retain their current business functionality, but this is not
yet a complete feature-for-feature port. Some Python workflows and page
details are still different or missing.

On the coffee edition's **Milling** page, the **Company stock milling:
Kibooko to Kasse** section is below the customer milling form. Enter input and
output weights and grades to record a company-stock conversion. The saved run
is retained in the business database, decreases Kibooko stock, increases Kasse
stock by the recorded output, and appears in company-milling reports and the
recent-runs table. Its separate **Company Milling** page remains available for
the mill's other company-owned coffee conversions.

Phone access is hosted by the Admin desktop. It starts automatically for
Admin by default, and creating a link code starts it if needed. Keep the
desktop app/server running. For secure phone linking and chat, start Tailscale
Serve on the desktop using the port shown in the generated phone-link dialog
(default `8765` for coffee and `8766` for grain):

```sh
tailscale serve 8765
```

Open the HTTPS URL printed by Tailscale Serve on the phone; use
`tailscale serve status` to see it again. HTTPS must be enabled for the tailnet.
The direct IP addresses shown in Settings use HTTP and are not suitable for
new phone linking. Entering the one-use eight-digit code automatically
assigns the role and account selected when that code was generated; the phone
does not ask the user to choose Admin or Manager. The system allows at most two
linked Admin devices and one linked Manager device.

An Admin phone is strictly view-only: it shows the dashboard and searchable
read-only business reports, while the server rejects every Admin phone record
write. A linked Manager phone retains record-entry access remotely while the
desktop/server remains online. The read-only report selector includes
purchases, sales, milling, expenses, payroll, taxes, credit, bank deposits,
balances, stock, cash flow, profit summary, and activity log.

After linking, scroll to the **Linked-device chat** panel at the top of the
phone page. Select a linked device from the conversation buttons or the
**Chat with** list, then type and send a message. On the desktop, open the
**Messages** page and click **Open secure chat**; Mill Manager opens a local
browser chat and links it automatically as a separate desktop participant.
Select a linked device to send messages and receive replies. Desktop chat does
not consume one of the two Admin or one Manager phone slots. The desktop app
must remain open while that browser chat is in use. Each conversation is
between the selected pair of devices; switching the selection opens that
device's separate message history. New messages are polled automatically.

Linked phones and the desktop browser participant can chat with any other
active linked device. New message bodies are encrypted on the sender device
with Web Crypto (P-256 ECDH and AES-GCM); only ciphertext is sent to and stored
by the desktop. Each linked device's non-exportable private key stays in that
browser's IndexedDB. Do not clear browser site data or move the linked account
to another browser: the private key cannot be recovered, and that device must
be removed and linked again.
Compare a contact's displayed key fingerprint with them over a separate
trusted channel. Old chat messages created by earlier versions are left intact
in the legacy local database table and are not migrated; only newly sent
messages use client-side encryption. The desktop serves the phone web page, so
a compromised desktop could serve modified client code; this design protects
new message content in the database and between honest clients, not against a
compromised desktop. Phones linked by an older version have no encryption key;
remove and link them again from the HTTPS Tailscale Serve URL before using chat.

Admins can choose a custom business logo in **Settings > Business logo**. It is
used on the app header, receipts, report print/PDF output, and Document Studio
exports/printing. Keep the selected image file available at the same path.

For printing, select the POS receipt printer and normal document printer under
**Settings > Receipt and document printers**. The app detects installed thermal
printers by common printer/model names, suggests 58 mm or 80 mm roll width,
and sends receipts directly to the selected POS queue using that paper width.
Use **Detect connected printers** after connecting a printer. Reports and
Document Studio use the normal printer setting and standard print dialog;
leave it blank to use the OS default. POS receipt page layout is tested through
a 58 mm PDF render in the self-test. Physical printer detection and paper
feeding still depend on the printer driver, connection, and installed queue.

The Linux package requires a Debian/Ubuntu amd64 system with Qt 6 runtime
packages and the SQLite driver. The package manager installs the declared
runtime dependencies.

`CPackSourceConfig.cmake` files are generated build artifacts and contain
machine-specific paths; do not copy one into the source tree. The project
generates its Windows installer configuration from `CMakeLists.txt`.
