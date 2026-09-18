export default {
  extends: ['@commitlint/config-conventional'],
  ignores: [(message) => /^merge\s/i.test(message)],
};
