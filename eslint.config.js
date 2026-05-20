export default [
    {
        files: ['src/gnome-shell/**/*.js'],
        languageOptions: {
            ecmaVersion: 2022,
            sourceType: 'module',
        },
        rules: {
            'no-unused-vars': ['error', { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }],
            'no-undef': 'off',
            'prefer-const': 'error',
            'no-var': 'error',
            'eqeqeq': ['error', 'smart'],
            'prefer-arrow-callback': 'error',
            'no-fallthrough': 'error',
        },
    },
];
