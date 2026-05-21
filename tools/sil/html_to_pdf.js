const { chromium } = require('playwright');
const path = require('path');
const fs = require('fs');

async function main() {
    const args = process.argv.slice(2);
    if (args.length < 2) {
        console.error('Usage: node html_to_pdf.js <input_html_path> <output_pdf_path>');
        process.exit(1);
    }

    const inputHtmlPath = path.resolve(args[0]);
    const outputPdfPath = path.resolve(args[1]);

    if (!fs.existsSync(inputHtmlPath)) {
        console.error(`Error: input file does not exist: ${inputHtmlPath}`);
        process.exit(1);
    }

    // Ensure target folder exists
    const outDir = path.dirname(outputPdfPath);
    if (!fs.existsSync(outDir)) {
        fs.mkdirSync(outDir, { recursive: true });
    }

    console.log(`Converting ${inputHtmlPath} to ${outputPdfPath}...`);

    const browser = await chromium.launch({ headless: true });
    try {
        const page = await browser.newPage();
        
        // Load the HTML file using the file:// protocol
        const fileUrl = `file://${inputHtmlPath}`;
        await page.goto(fileUrl, { waitUntil: 'networkidle' });

        // Generate PDF in standard A4 format, preserving backgrounds
        await page.pdf({
            path: outputPdfPath,
            format: 'A4',
            printBackground: true,
            margin: {
                top: '20mm',
                bottom: '20mm',
                left: '20mm',
                right: '20mm'
            }
        });
        
        console.log(`Success! PDF generated at ${outputPdfPath}`);
    } catch (err) {
        console.error('Error during PDF generation:', err);
        process.exit(1);
    } finally {
        await browser.close();
    }
}

main();
