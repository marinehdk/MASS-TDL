import { useState, useEffect, useRef } from 'react';
import Ajv from 'ajv';
import * as jsyaml from 'js-yaml';

export interface SchemaValidationResult {
  valid: boolean;
  errors: string[];
}

let cachedSchema: object | null = null;
let ajvInstance: Ajv | null = null;

async function loadSchema(): Promise<object> {
  if (cachedSchema) return cachedSchema;
  const response = await fetch('/api/v1/schema/fcb_traffic_situation');
  if (!response.ok) throw new Error(`Schema fetch failed: ${response.status}`);
  cachedSchema = await response.json();
  return cachedSchema!;
}

function getAjv(): Ajv {
  if (!ajvInstance) {
    ajvInstance = new Ajv({ allErrors: true, verbose: true });
    // Allow additional properties in metadata extension
    ajvInstance.addKeyword('additionalProperties');
  }
  return ajvInstance;
}

export function useSchemaValidation(yamlContent: string): SchemaValidationResult {
  const [result, setResult] = useState<SchemaValidationResult>({ valid: true, errors: [] });
  const lastContentRef = useRef<string>('');

  useEffect(() => {
    if (!yamlContent.trim()) {
      if (lastContentRef.current !== '') {
        setResult({ valid: false, errors: ['YAML content is empty'] });
        lastContentRef.current = '';
      }
      return;
    }

    let cancelled = false;
    lastContentRef.current = yamlContent;

    loadSchema()
      .then(schema => {
        if (cancelled) return;

        try {
          const doc = jsyaml.load(yamlContent);
          if (!doc || typeof doc !== 'object') {
            setResult({ valid: false, errors: ['YAML must be a valid object'] });
            return;
          }

          const ajv = getAjv();
          const validate = ajv.compile(schema as any);
          const valid = validate(doc);

          const errors: string[] = [];
          if (!valid && validate.errors) {
            for (const err of validate.errors) {
              const path = err.instancePath || '(root)';
              errors.push(`${path}: ${err.message || 'validation failed'}`);
            }
          }

          setResult({ valid, errors });
        } catch (e: any) {
          setResult({ valid: false, errors: [`YAML parse error: ${e.message}`] });
        }
      })
      .catch((e: any) => {
        if (!cancelled) {
          setResult({ valid: false, errors: [`Schema load error: ${e.message}`] });
        }
      });

    return () => { cancelled = true; };
  }, [yamlContent]);

  return result;
}
